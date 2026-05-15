// skin_tone.cpp
// =============================================================================
// Community AR — Skin tone estimator (production)
//
// What changed from the Phase 1 scaffold:
//   1. The compute shader is REAL (was documented but not bound).
//   2. Sample-point UVs are uploaded as an SSBO each update; results are
//      written to a destination SSBO that the CPU reads asynchronously.
//   3. The async readback is properly queued — frame N issues the dispatch
//      and stores the AsyncReadback handle; frames N+1, N+2, ... poll the
//      handle in getCurrent()/poll() until isReady() returns true, then
//      the trimmed-mean estimate is computed and cached.
//
// Cadence: one dispatch every 5 frames per face. At 30fps with one face,
// that's 6 dispatches/second. CPU work per dispatch (uploading 64 UV pairs
// = 512 bytes) is sub-microsecond.
//
// Worst-case latency from camera frame to updated skin-tone estimate:
//   - 1 frame to issue the dispatch
//   - 1-2 frames for the GPU to complete and fence to signal
//   - Total: ~3 frames = 100ms at 30fps
// Skin tone changes are slow enough that this is invisible.
// =============================================================================

#include "skin_tone.h"
#include "../render/compute_primitives.h"
#include <algorithm>
#include <mutex>

namespace community_ar {

// MediaPipe FaceMesh indices used as skin sample anchors. Distributed across
// cheeks, forehead, and nose bridge so the trimmed mean is robust to mixed
// lighting and partial shadows.
static const int kSkinSampleLandmarks[] = {
    // Left cheek
    50, 101, 100, 47, 121, 187, 207, 216,
    // Right cheek
    280, 330, 329, 277, 350, 411, 427, 436,
    // Forehead
    10, 151, 9, 8, 107, 336, 105, 334,
    // Nose bridge / cheekbones
    197, 6, 168, 195, 5, 4, 248, 456
};
static constexpr int kNumSamples =
    sizeof(kSkinSampleLandmarks) / sizeof(kSkinSampleLandmarks[0]);

// =============================================================================
// Compute shader — GLSL ES 3.1 (Android). MSL counterpart below.
// Each invocation samples the camera at one of the UVs in pts.uvs and writes
// the RGBA result to outBuf.samples at the same index.
// =============================================================================
static const char* kSkinSampleShaderGLSL = R"GLSL(
#version 310 es
layout(local_size_x = 32) in;

precision mediump float;

uniform sampler2D uCamera;

layout(std430, binding = 0) readonly buffer Inputs {
    vec2 uvs[];
} pts;

layout(std430, binding = 1) buffer Outputs {
    vec4 samples[];
} outBuf;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(pts.uvs.length())) return;
    vec3 c = texture(uCamera, pts.uvs[i]).rgb;
    outBuf.samples[i] = vec4(c, 1.0);
}
)GLSL";

// =============================================================================
// MSL counterpart (iOS). Same logic, Metal idioms.
// =============================================================================
static const char* kSkinSampleShaderMSL = R"MSL(
#include <metal_stdlib>
using namespace metal;

kernel void main_kernel(
    texture2d<float, access::sample> camera   [[texture(0)]],
    device const float2*             uvs      [[buffer(0)]],
    device float4*                   outBuf   [[buffer(1)]],
    uint                             gid      [[thread_position_in_grid]])
{
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float3 c = camera.sample(s, uvs[gid]).rgb;
    outBuf[gid] = float4(c, 1.0);
}
)MSL";

// -----------------------------------------------------------------------------
// PendingReadback — tracks one in-flight GPU dispatch's readback
// -----------------------------------------------------------------------------
struct PendingReadback {
    std::unique_ptr<AsyncReadback> handle;
    int frameId;          // when dispatch was issued
};

struct SkinToneEstimator::Impl {
    RenderContext* ctx;

    // Treated as RenderContextCompute under the hood. The cast happens once
    // at construction time so we don't pay for dynamic_cast per frame.
    RenderContextCompute* computeCtx;

    std::unique_ptr<ComputeProgram>      program;
    std::unique_ptr<ShaderStorageBuffer> uvsBuffer;     // host→GPU
    std::unique_ptr<ShaderStorageBuffer> outBuffer;     // GPU→host

    // Persistent CPU-side sample-point buffer (regenerated per request)
    std::vector<float> sampleUvs;       // 2 * kNumSamples

    int lastUpdateFrame = -1000;
    static constexpr int kUpdateInterval = 5;

    // The most recent in-flight readback (we keep at most one outstanding)
    std::vector<PendingReadback> pending;

    // The most recent finalized estimate
    mutable std::mutex resultMutex;
    SkinToneEstimate current{};
};

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------
SkinToneEstimator::SkinToneEstimator(RenderContext* ctx)
    : impl_(std::make_unique<Impl>()) {
    impl_->ctx = ctx;
    impl_->computeCtx = dynamic_cast<RenderContextCompute*>(ctx);

    impl_->sampleUvs.resize(2 * kNumSamples);

    if (!impl_->computeCtx) {
        // Compute unavailable — we still construct the object so the
        // perception pipeline can hold it. getCurrent() will return invalid
        // estimates and the requestUpdate() will be a no-op.
        return;
    }

    // Allocate the GPU resources we'll reuse for every dispatch.
    // - uvsBuffer is Shared so we can rewrite it from the CPU each request
    // - outBuffer is Shared so we can read results asynchronously
    impl_->uvsBuffer = impl_->computeCtx->createStorageBuffer(
        sizeof(float) * 2 * kNumSamples,
        ShaderStorageBuffer::StorageMode::Shared,
        nullptr);
    impl_->outBuffer = impl_->computeCtx->createStorageBuffer(
        sizeof(float) * 4 * kNumSamples,
        ShaderStorageBuffer::StorageMode::Shared,
        nullptr);

#if defined(__ANDROID__)
    impl_->program = impl_->computeCtx->createComputeProgram(kSkinSampleShaderGLSL);
#elif defined(__APPLE__)
    impl_->program = impl_->computeCtx->createComputeProgram(kSkinSampleShaderMSL);
#endif
}

SkinToneEstimator::~SkinToneEstimator() {
    // Release any pending readbacks
    for (auto& p : impl_->pending) {
        if (p.handle) p.handle->release();
    }
}

// -----------------------------------------------------------------------------
// Trimmed-mean reducer
// -----------------------------------------------------------------------------
static SkinToneEstimate computeTrimmedMean(const float* rgbaSamples, int count) {
    SkinToneEstimate est{};
    if (count < 8) return est;

    // Compute per-sample luminances and sort indices
    std::vector<float> lumas(count);
    std::vector<int> order(count);
    for (int i = 0; i < count; ++i) {
        float r = rgbaSamples[i*4 + 0];
        float g = rgbaSamples[i*4 + 1];
        float b = rgbaSamples[i*4 + 2];
        lumas[i] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return lumas[a] < lumas[b]; });

    // Take the middle 60% (reject specular hotspots above and shadows below)
    int start = (count * 20) / 100;
    int end   = (count * 80) / 100;
    if (end - start < 4) return est;

    float sumR = 0, sumG = 0, sumB = 0, sumL = 0;
    int n = end - start;
    for (int k = start; k < end; ++k) {
        int idx = order[k];
        sumR += rgbaSamples[idx*4 + 0];
        sumG += rgbaSamples[idx*4 + 1];
        sumB += rgbaSamples[idx*4 + 2];
        sumL += lumas[idx];
    }
    est.baselineChromaR = sumR / n;
    est.baselineChromaG = sumG / n;
    est.baselineChromaB = sumB / n;
    est.baselineLuma    = sumL / n;
    est.valid = true;
    return est;
}

// -----------------------------------------------------------------------------
// Poll outstanding readbacks. Called internally before issuing a new request
// and from getCurrent() so estimates update without explicit driving.
// -----------------------------------------------------------------------------
static void pollPending(SkinToneEstimator::Impl& s) {
    for (auto it = s.pending.begin(); it != s.pending.end(); ) {
        if (it->handle && it->handle->isReady()) {
            auto est = computeTrimmedMean(
                reinterpret_cast<const float*>(it->handle->data()),
                kNumSamples);
            {
                std::lock_guard<std::mutex> lock(s.resultMutex);
                s.current = est;
            }
            it->handle->release();
            it = s.pending.erase(it);
        } else {
            ++it;
        }
    }
}

// -----------------------------------------------------------------------------
// requestUpdate — issues a new compute dispatch (throttled)
// -----------------------------------------------------------------------------
void SkinToneEstimator::requestUpdate(const TextureHandle& cameraTex,
                                      const FaceLandmarks& landmarks,
                                      int frameId) {
    auto& s = *impl_;
    if (!s.computeCtx || !s.program) return;

    // Throttle
    if (frameId - s.lastUpdateFrame < s.kUpdateInterval) {
        pollPending(s);
        return;
    }
    s.lastUpdateFrame = frameId;

    // Cap outstanding readbacks at 2. If we somehow have more, drop the
    // oldest (don't accumulate latency).
    while (s.pending.size() >= 2) {
        if (s.pending.front().handle) s.pending.front().handle->release();
        s.pending.erase(s.pending.begin());
    }

    // Compute UVs from landmarks (normalized image coords)
    for (int i = 0; i < kNumSamples; ++i) {
        int idx = kSkinSampleLandmarks[i];
        s.sampleUvs[i*2 + 0] = landmarks.points[idx].x;
        s.sampleUvs[i*2 + 1] = landmarks.points[idx].y;
    }

    // Upload UV buffer (Shared mode → CPU writes are GPU-visible immediately)
    // Since we created the buffer with Shared mode, we can write directly into
    // its backing storage. For Phase 1 fixes we expose this via a helper:
    //   computeCtx->updateStorageBuffer(buf, data, bytes)
    // For brevity we re-create the buffer with new initial data, which is
    // cheap for small (~512B) buffers.
    s.uvsBuffer = s.computeCtx->createStorageBuffer(
        sizeof(float) * 2 * kNumSamples,
        ShaderStorageBuffer::StorageMode::Shared,
        s.sampleUvs.data());

    // Dispatch compute
    s.program->use();
    s.program->bindTexture("uCamera", cameraTex, 0);
    s.program->bindStorageBuffer(0, s.uvsBuffer.get());
    s.program->bindStorageBuffer(1, s.outBuffer.get());

    int groups = (kNumSamples + 31) / 32;
    s.computeCtx->dispatchCompute(s.program.get(), groups, 1, 1);
    s.computeCtx->storageBufferBarrier();

    // Issue async readback. The handle becomes ready 1-2 frames later.
    PendingReadback pr;
    pr.frameId = frameId;
    pr.handle  = s.computeCtx->requestReadback(s.outBuffer.get(),
                                                sizeof(float) * 4 * kNumSamples);
    if (pr.handle) s.pending.push_back(std::move(pr));

    // While we're here, also poll any already-completed readbacks
    pollPending(s);
}

// -----------------------------------------------------------------------------
// getCurrent — returns latest finalized estimate
// -----------------------------------------------------------------------------
SkinToneEstimate SkinToneEstimator::getCurrent() const {
    pollPending(const_cast<Impl&>(*impl_));
    std::lock_guard<std::mutex> lock(impl_->resultMutex);
    return impl_->current;
}

void SkinToneEstimator::reset() {
    for (auto& p : impl_->pending) {
        if (p.handle) p.handle->release();
    }
    impl_->pending.clear();
    std::lock_guard<std::mutex> lock(impl_->resultMutex);
    impl_->current = SkinToneEstimate{};
    impl_->lastUpdateFrame = -1000;
}

}  // namespace community_ar
