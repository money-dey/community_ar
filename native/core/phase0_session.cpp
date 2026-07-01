// phase0_session.cpp
// =============================================================================
// Implementation. The flow per frame is intentionally trivial:
//
//   1. Receive camera texture handle from platform adapter
//   2. (Re)allocate output framebuffer if dimensions changed
//   3. Bind output framebuffer
//   4. Bind the active test shader
//   5. Bind the camera texture as input
//   6. Draw a full-screen quad
//   7. Flush
//
// The output framebuffer's color texture handle is what gets published to
// Flutter via the platform's TextureRegistry.
// =============================================================================

#include "phase0_session.h"
#include "phase0_session_phase2.h"  // complete Phase2Members for p2_ lifetime
#include <chrono>

namespace community_ar {

// -----------------------------------------------------------------------------
// Shader sources — written in GLSL ES 3.00 for Android; we'll cross-compile
// to MSL for iOS via SPIRV-Cross in a later iteration. For Phase 0, we ship
// both sources hand-written and let the RenderContext pick the right one.
// -----------------------------------------------------------------------------
static const char* kPassthroughVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)";

static const char* kPassthroughFS = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = texture(uTex, vUv); }
)";

static const char* kGrayscaleFS = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
in vec2 vUv;
out vec4 fragColor;
void main() {
    vec3 c = texture(uTex, vUv).rgb;
    float g = dot(c, vec3(0.2126, 0.7152, 0.0722));
    fragColor = vec4(vec3(g), 1.0);
}
)";

static const char* kInvertFS = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
in vec2 vUv;
out vec4 fragColor;
void main() {
    vec3 c = texture(uTex, vUv).rgb;
    fragColor = vec4(1.0 - c, 1.0);
}
)";

static const char* kVignetteFS = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
in vec2 vUv;
out vec4 fragColor;
void main() {
    vec3 c = texture(uTex, vUv).rgb;
    vec2 d = vUv - vec2(0.5);
    float vig = 1.0 - smoothstep(0.3, 0.7, length(d));
    fragColor = vec4(c * vig, 1.0);
}
)";

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------
Phase0Session::Phase0Session(const CARPhase0Config& cfg)
    : p2_(std::make_unique<Phase2Members>()) {
#if defined(__ANDROID__)
    if (cfg.backend == CAR_BACKEND_GLES) {
        ctx_ = createGlesRenderContext(
            reinterpret_cast<void*>(cfg.gpuContext),
            reinterpret_cast<void*>(cfg.gpuDisplay));
    }
#elif defined(__APPLE__)
    if (cfg.backend == CAR_BACKEND_METAL) {
        ctx_ = createMetalRenderContext(reinterpret_cast<void*>(cfg.gpuContext));
    }
#endif

    if (!ctx_) {
        // Caller will see this as a null Session; for Phase 0 we keep it simple
        return;
    }

    ensureShaders();
}

Phase0Session::~Phase0Session() = default;

void Phase0Session::ensureShaders() {
    shaderPassthrough_ = ctx_->createShader(kPassthroughVS, kPassthroughFS);
    shaderGrayscale_   = ctx_->createShader(kPassthroughVS, kGrayscaleFS);
    shaderInvert_      = ctx_->createShader(kPassthroughVS, kInvertFS);
    shaderVignette_    = ctx_->createShader(kPassthroughVS, kVignetteFS);
}

void Phase0Session::ensureOutputFramebuffer(int w, int h) {
    if (!outputFbo_ || outputWidth_ != w || outputHeight_ != h) {
        outputFbo_ = ctx_->createFramebuffer(w, h, TextureHandle::Format::RGBA8);
        outputWidth_ = w;
        outputHeight_ = h;
    }
}

// -----------------------------------------------------------------------------
// Frame submission — runs on platform camera callback thread
// -----------------------------------------------------------------------------
CARStatus Phase0Session::submitFrame(uint64_t cameraTextureHandle,
                                     int width, int height,
                                     int rotationDegrees,
                                     bool isFrontFacing) {
    if (!ctx_ || cameraTextureHandle == 0 || width <= 0 || height <= 0) {
        return CAR_STATUS_INTERNAL_ERROR;
    }

    auto t0 = std::chrono::steady_clock::now();
    processFrame(cameraTextureHandle, width, height, rotationDegrees, isFrontFacing);
    auto t1 = std::chrono::steady_clock::now();

    float frameMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        // Exponential moving average
        avgFrameTimeMs_ = avgFrameTimeMs_ * 0.95f + frameMs * 0.05f;
        framesProcessed_++;
    }
    return CAR_STATUS_OK;
}

void Phase0Session::processFrame(uint64_t cameraTexHandle, int w, int h,
                                 int rotation, bool isFrontFacing) {
    // Account for rotation: for portrait camera output (rotation = 90), we
    // swap output dimensions so the displayed image is upright.
    int outW = w, outH = h;
    if (rotation == 90 || rotation == 270) std::swap(outW, outH);

    std::lock_guard<std::mutex> lock(outputMutex_);
    ensureOutputFramebuffer(outW, outH);

    // Wrap the borrowed camera texture handle. On Android this is an OES
    // external texture; on iOS it's a Metal texture from CVMetalTextureCache.
    TextureHandle cameraTex(
        cameraTexHandle, w, h,
#if defined(__ANDROID__)
        TextureHandle::Format::ExternalOES,
#else
        TextureHandle::Format::RGBA8,
#endif
        TextureHandle::Ownership::Borrowed);

    ctx_->bindFramebuffer(outputFbo_.get());
    ctx_->clearColor(0.0f, 0.0f, 0.0f, 1.0f);

    ShaderProgram* shader = shaderPassthrough_.get();
    switch (testMode_.load()) {
        case CAR_TEST_MODE_PASSTHROUGH: shader = shaderPassthrough_.get(); break;
        case CAR_TEST_MODE_GRAYSCALE:   shader = shaderGrayscale_.get();   break;
        case CAR_TEST_MODE_INVERT:      shader = shaderInvert_.get();      break;
        case CAR_TEST_MODE_VIGNETTE:    shader = shaderVignette_.get();    break;
    }

    shader->use();
    shader->bindTexture("uTex", cameraTex, 0);
    ctx_->drawFullscreenQuad(shader);
    ctx_->flush();
}

// -----------------------------------------------------------------------------
// Output access — any thread
// -----------------------------------------------------------------------------
uint64_t Phase0Session::getOutputTexture() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    return outputFbo_ ? outputFbo_->colorTexture().nativeHandle() : 0;
}

void Phase0Session::getOutputDimensions(int* outW, int* outH) const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    if (outW) *outW = outputWidth_;
    if (outH) *outH = outputHeight_;
}

CARStatus Phase0Session::setTestMode(CARTestMode mode) {
    testMode_.store(mode);
    return CAR_STATUS_OK;
}

void Phase0Session::getStats(CARPhase0Stats* outStats) const {
    if (!outStats) return;
    std::lock_guard<std::mutex> lock(statsMutex_);
    outStats->avgFrameTimeMs = avgFrameTimeMs_;
    outStats->framesProcessed = framesProcessed_;
    outStats->framesDropped = framesDropped_;
}

}  // namespace community_ar
