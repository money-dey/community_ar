// phase0_session.h
// =============================================================================
// Community AR — Phase 0 Session
//
// Minimal Session: owns the RenderContext, an output framebuffer, and a
// trivial test shader. No perception, no effect graph yet — just enough
// to prove the data highway works.
// =============================================================================

#pragma once

#include "ffi/community_ar_phase0_api.h"
#include "render/render_context.h"
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>

namespace community_ar {

class EffectGraph;
class PerceptionPipeline;
class NeuralBackend;

class Phase0Session {
public:
    explicit Phase0Session(const CARPhase0Config& cfg);
    ~Phase0Session();

    // Called from platform camera callback thread
    CARStatus submitFrame(uint64_t cameraTextureHandle,
                          int width, int height,
                          int rotationDegrees,
                          bool isFrontFacing);

    // Display present path (Option A, docs/ANDROID_RENDER_PIPELINE.md): renders
    // the camera OES texture straight into the default framebuffer (fbo 0 == the
    // platform window surface) applying `texMatrix16` (column-major 4x4, may be
    // null = identity) to the quad UVs. The caller owns the window surface and
    // presents with eglSwapBuffers afterwards. Must run on the render thread
    // whose current EGL context owns fbo 0.
    CARStatus submitFrameToDisplay(uint64_t cameraTextureHandle,
                                   int width, int height,
                                   int rotationDegrees,
                                   bool isFrontFacing,
                                   const float* texMatrix16);

    // AR display path (WP-B, docs/AR_INTEGRATION_SPEC.md §3). Per frame:
    //   1. drain the render queue (graph installs take effect this frame)
    //   2. ingress-copy the camera OES texture into a 2D texture, applying
    //      `texMatrix16` — so perception + effects see an upright, zoomed frame
    //      and sample plain sampler2D (cameraOutputTexture() returns it)
    //   3. graph installed → renderFramePhase2() into the offscreen output FBO,
    //      then blit that to the default framebuffer (the window surface);
    //      graph empty → present the ingress texture through the Phase 0
    //      test-mode shader (preserves Phase 0 behaviour exactly)
    // The caller (Kotlin GL thread) presents with eglSwapBuffers afterwards.
    // `captureTimestampNs` is the SurfaceTexture frame timestamp, consumed by
    // perception + the One-Euro filters. Render thread only.
    CARStatus submitFrameAR(uint64_t cameraTextureHandle,
                            int width, int height,
                            const float* texMatrix16,
                            int64_t captureTimestampNs);

    // Called from any thread; just returns the handle
    uint64_t getOutputTexture() const;
    void     getOutputDimensions(int* outW, int* outH) const;

    // Set which trivial test shader to run
    CARStatus setTestMode(CARTestMode mode);

    void getStats(CARPhase0Stats* outStats) const;

    // ---- Phase 2 integration ----
    // Accessors and the render path used by the Phase 2/3 C ABI. All lazily
    // create their backing objects on first use (see
    // phase0_session_phase2_updates.cpp).
    RenderContext*       renderContext();
    NeuralBackend*       neuralBackend();
    EffectGraph&         effectGraph();

    // Non-constructing peek at the installed-effect count. The effectGraph()
    // accessor lazily constructs, which is only safe on the render thread;
    // this is callable from any thread for diagnostics (returns 0 when no
    // graph has ever been installed).
    size_t               installedEffectCount() const;
    PerceptionPipeline&  perceptionPipeline();
    const TextureHandle& cameraOutputTexture();
    Framebuffer*         displayFramebuffer();

    // Queue a task to run on the render thread before the next frame. Used by
    // the ABI to make graph mutations thread-safe without exposing locks.
    void runOnRenderThread(std::function<void()> task);
    void drainRenderQueue();

    // Per-frame render path with perception + effect graph (replaces the
    // Phase 0 test-shader path when an effect graph is installed).
    void renderFramePhase2(int64_t captureTimestampNs);

private:
    void processFrame(uint64_t cameraTexHandle, int w, int h,
                      int rotation, bool isFrontFacing);
    void processFrameToDisplay(uint64_t cameraTexHandle, int w, int h,
                               int rotation, bool isFrontFacing,
                               const float* texMatrix16);
    void processFrameAR(uint64_t cameraTexHandle, int w, int h,
                        const float* texMatrix16, int64_t captureTimestampNs);

    void ensureOutputFramebuffer(int w, int h);
    void ensureIngressFramebuffer(int w, int h);
    void ensureShaders();

    std::unique_ptr<RenderContext> ctx_;
    std::unique_ptr<Framebuffer>   outputFbo_;

    // Camera ingress target (WP-B): the OES camera frame is copied here each
    // frame with the UV transform applied; its color texture is what
    // cameraOutputTexture() hands to perception + the effect graph.
    std::unique_ptr<Framebuffer>   ingressFbo_;
    int outputWidth_ = 0;
    int outputHeight_ = 0;

    // Test shaders for Phase 0 smoke testing
    std::unique_ptr<ShaderProgram> shaderPassthrough_;
    std::unique_ptr<ShaderProgram> shaderGrayscale_;
    std::unique_ptr<ShaderProgram> shaderInvert_;
    std::unique_ptr<ShaderProgram> shaderVignette_;

    // Display-path counterparts: same effects but sampling the camera as an
    // external-OES texture (samplerExternalOES) and applying the UV transform.
    // Used by submitFrameToDisplay; the sampler2D shaders above stay for the
    // offscreen submitFrame path (which samples an already-copied 2D texture).
    std::unique_ptr<ShaderProgram> shaderOesPassthrough_;
    std::unique_ptr<ShaderProgram> shaderOesGrayscale_;
    std::unique_ptr<ShaderProgram> shaderOesInvert_;
    std::unique_ptr<ShaderProgram> shaderOesVignette_;

    std::atomic<CARTestMode> testMode_{CAR_TEST_MODE_PASSTHROUGH};

    // Diagnostics
    mutable std::mutex statsMutex_;
    float  avgFrameTimeMs_ = 0.0f;
    int    framesProcessed_ = 0;
    int    framesDropped_ = 0;

    // Concurrency: submitFrame can be called from camera thread while
    // getOutputTexture is called from Flutter's render thread.
    mutable std::mutex outputMutex_;

    // Phase 2 additions (effect graph, perception pipeline, neural backend,
    // render-thread queue). Defined in phase0_session_phase2.h; allocated in
    // the constructor. Kept behind a pointer so the Phase 0 header stays free
    // of the Phase 2/3 includes.
    struct Phase2Members;
    std::unique_ptr<Phase2Members> p2_;
};

}  // namespace community_ar
