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
// Display-path shaders (Option A, docs/ANDROID_RENDER_PIPELINE.md).
//
// The camera texture is a GL_TEXTURE_EXTERNAL_OES texture, so these must sample
// it with samplerExternalOES (needs the essl3 external-image extension) rather
// than sampler2D. The vertex shader also applies uTexMatrix — the combined
// SurfaceTexture-transform × rotation/mirror matrix supplied per frame — to the
// quad UVs, which is how orientation/crop/flip are handled without touching the
// output buffer geometry.
// -----------------------------------------------------------------------------
static const char* kOesVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uTexMatrix;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = (uTexMatrix * vec4(aUv, 0.0, 1.0)).xy;
}
)";

static const char* kOesPassthroughFS = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
uniform samplerExternalOES uTex;
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = texture(uTex, vUv); }
)";

static const char* kOesGrayscaleFS = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
uniform samplerExternalOES uTex;
in vec2 vUv;
out vec4 fragColor;
void main() {
    vec3 c = texture(uTex, vUv).rgb;
    float g = dot(c, vec3(0.2126, 0.7152, 0.0722));
    fragColor = vec4(vec3(g), 1.0);
}
)";

static const char* kOesInvertFS = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
uniform samplerExternalOES uTex;
in vec2 vUv;
out vec4 fragColor;
void main() {
    vec3 c = texture(uTex, vUv).rgb;
    fragColor = vec4(1.0 - c, 1.0);
}
)";

static const char* kOesVignetteFS = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
uniform samplerExternalOES uTex;
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

    shaderOesPassthrough_ = ctx_->createShader(kOesVS, kOesPassthroughFS);
    shaderOesGrayscale_   = ctx_->createShader(kOesVS, kOesGrayscaleFS);
    shaderOesInvert_      = ctx_->createShader(kOesVS, kOesInvertFS);
    shaderOesVignette_    = ctx_->createShader(kOesVS, kOesVignetteFS);
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
// Frame submission (display present path) — runs on the render thread
// -----------------------------------------------------------------------------
CARStatus Phase0Session::submitFrameToDisplay(uint64_t cameraTextureHandle,
                                              int width, int height,
                                              int rotationDegrees,
                                              bool isFrontFacing,
                                              const float* texMatrix16) {
    if (!ctx_ || cameraTextureHandle == 0 || width <= 0 || height <= 0) {
        return CAR_STATUS_INTERNAL_ERROR;
    }

    auto t0 = std::chrono::steady_clock::now();
    processFrameToDisplay(cameraTextureHandle, width, height, rotationDegrees,
                          isFrontFacing, texMatrix16);
    auto t1 = std::chrono::steady_clock::now();

    float frameMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        avgFrameTimeMs_ = avgFrameTimeMs_ * 0.95f + frameMs * 0.05f;
        framesProcessed_++;
    }
    return CAR_STATUS_OK;
}

void Phase0Session::processFrameToDisplay(uint64_t cameraTexHandle,
                                          int w, int h, int /*rotation*/,
                                          bool /*isFrontFacing*/,
                                          const float* texMatrix16) {
    // The caller (Kotlin/EGL layer) has already resolved orientation into
    // texMatrix16 and passes the final display dimensions in w/h, so — unlike
    // the offscreen processFrame path — we do NOT swap w/h here.
    //
    // Identity fallback so a null matrix still samples the quad 1:1.
    static const float kIdentity[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
    };
    const float* texMatrix = texMatrix16 ? texMatrix16 : kIdentity;

    std::lock_guard<std::mutex> lock(outputMutex_);
    outputWidth_ = w;
    outputHeight_ = h;

    // Borrowed OES external texture from Camera2's SurfaceTexture. Because the
    // camera texture is created on the same EGL context we render on (Option A),
    // this raw GL name is valid here.
    TextureHandle cameraTex(cameraTexHandle, w, h,
                            TextureHandle::Format::ExternalOES,
                            TextureHandle::Ownership::Borrowed);

    // Render straight into fbo 0 — the platform window surface. Kotlin swaps.
    ctx_->bindDisplayFramebuffer(w, h);
    ctx_->clearColor(0.0f, 0.0f, 0.0f, 1.0f);

    ShaderProgram* shader = shaderOesPassthrough_.get();
    switch (testMode_.load()) {
        case CAR_TEST_MODE_PASSTHROUGH: shader = shaderOesPassthrough_.get(); break;
        case CAR_TEST_MODE_GRAYSCALE:   shader = shaderOesGrayscale_.get();   break;
        case CAR_TEST_MODE_INVERT:      shader = shaderOesInvert_.get();      break;
        case CAR_TEST_MODE_VIGNETTE:    shader = shaderOesVignette_.get();    break;
    }

    shader->use();
    shader->setUniformMatrix4("uTexMatrix", texMatrix);
    shader->bindTexture("uTex", cameraTex, 0);
    ctx_->drawFullscreenQuad(shader);
    ctx_->flush();
}

// -----------------------------------------------------------------------------
// Frame submission (AR path) — runs on the render thread
//
// This subsumes the Phase 0 display path: with no effect graph installed it
// produces pixel-identical output (camera through the test-mode shader), so the
// platform layer can call it unconditionally once it exists. See
// docs/AR_INTEGRATION_SPEC.md §3 for the ingress/present design.
// -----------------------------------------------------------------------------
CARStatus Phase0Session::submitFrameAR(uint64_t cameraTextureHandle,
                                       int width, int height,
                                       const float* texMatrix16,
                                       int64_t captureTimestampNs) {
    if (!ctx_ || cameraTextureHandle == 0 || width <= 0 || height <= 0) {
        return CAR_STATUS_INTERNAL_ERROR;
    }

    auto t0 = std::chrono::steady_clock::now();
    processFrameAR(cameraTextureHandle, width, height, texMatrix16,
                   captureTimestampNs);
    auto t1 = std::chrono::steady_clock::now();

    float frameMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        avgFrameTimeMs_ = avgFrameTimeMs_ * 0.95f + frameMs * 0.05f;
        framesProcessed_++;
    }
    return CAR_STATUS_OK;
}

void Phase0Session::ensureIngressFramebuffer(int w, int h) {
    if (!ingressFbo_ || ingressFbo_->width() != w || ingressFbo_->height() != h) {
        ingressFbo_ = ctx_->createFramebuffer(w, h, TextureHandle::Format::RGBA8);
    }
}

void Phase0Session::processFrameAR(uint64_t cameraTexHandle, int w, int h,
                                   const float* texMatrix16,
                                   int64_t captureTimestampNs) {
    static const float kIdentity[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
    };
    const float* texMatrix = texMatrix16 ? texMatrix16 : kIdentity;

    std::lock_guard<std::mutex> lock(outputMutex_);
    outputWidth_ = w;
    outputHeight_ = h;

    // 1. Drain queued ABI work (graph swaps, perception-requirement updates)
    //    BEFORE deciding which path runs, so a graph installed from the
    //    platform thread takes effect on this very frame.
    drainRenderQueue();

    // 2. Camera ingress: OES → 2D, applying the UV transform exactly once.
    //    Everything downstream — perception models, effect shaders, the present
    //    blit — samples an upright, digitally-zoomed sampler2D frame. This is
    //    the single place orientation is applied (double-applying would rotate
    //    the image twice; see AR_INTEGRATION_SPEC.md §5).
    ensureIngressFramebuffer(w, h);
    TextureHandle cameraOes(cameraTexHandle, w, h,
                            TextureHandle::Format::ExternalOES,
                            TextureHandle::Ownership::Borrowed);
    ctx_->bindFramebuffer(ingressFbo_.get());
    ctx_->clearColor(0.0f, 0.0f, 0.0f, 1.0f);
    shaderOesPassthrough_->use();
    shaderOesPassthrough_->setUniformMatrix4("uTexMatrix", texMatrix);
    shaderOesPassthrough_->bindTexture("uTex", cameraOes, 0);
    ctx_->drawFullscreenQuad(shaderOesPassthrough_.get());

    // 3. Pointer-check the graph (don't use the effectGraph() accessor here —
    //    it lazily constructs, and the empty-graph hot path shouldn't allocate
    //    Phase 2 machinery that nothing has asked for yet).
    const bool hasGraph = p2_->effectGraph && p2_->effectGraph->effectCount() > 0;

    // WP-E: an active debug overlay / forced perception also needs the
    // Phase 2 path — with an empty graph it degenerates to a camera blit with
    // the overlay composited on top, which is exactly what "show me the
    // landmarks with no effect installed" means. Allocating the Phase 2
    // machinery is fine here: debugging asked for it explicitly.
    const bool debugActive =
        p2_->debugOverlayMask.load(std::memory_order_relaxed) != 0 ||
        p2_->forcedPerceptionBits.load(std::memory_order_relaxed) != 0;

    if (hasGraph || debugActive) {
        // 3a. Perception + effect graph render into the offscreen output FBO
        //     (effects need it for ping-pong chaining).
        ensureOutputFramebuffer(w, h);
        renderFramePhase2(captureTimestampNs);

        // 3b. Present: offscreen result → default framebuffer (the EGL window
        //     surface). Kotlin swaps buffers after this returns.
        ctx_->bindDisplayFramebuffer(w, h);
        shaderPassthrough_->use();
        shaderPassthrough_->bindTexture("uTex", outputFbo_->colorTexture(), 0);
        ctx_->drawFullscreenQuad(shaderPassthrough_.get());
    } else {
        // 3c. No effects: present the ingress texture through the active
        //     test-mode shader (the sampler2D variants — ingress is 2D now).
        //     Pixel-identical to the Phase 0 display path.
        ctx_->bindDisplayFramebuffer(w, h);
        ShaderProgram* shader = shaderPassthrough_.get();
        switch (testMode_.load()) {
            case CAR_TEST_MODE_PASSTHROUGH: shader = shaderPassthrough_.get(); break;
            case CAR_TEST_MODE_GRAYSCALE:   shader = shaderGrayscale_.get();   break;
            case CAR_TEST_MODE_INVERT:      shader = shaderInvert_.get();      break;
            case CAR_TEST_MODE_VIGNETTE:    shader = shaderVignette_.get();    break;
        }
        shader->use();
        shader->bindTexture("uTex", ingressFbo_->colorTexture(), 0);
        ctx_->drawFullscreenQuad(shader);
    }
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
