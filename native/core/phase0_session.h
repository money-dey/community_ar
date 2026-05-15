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

namespace community_ar {

class Phase0Session {
public:
    explicit Phase0Session(const CARPhase0Config& cfg);
    ~Phase0Session();

    // Called from platform camera callback thread
    CARStatus submitFrame(uint64_t cameraTextureHandle,
                          int width, int height,
                          int rotationDegrees,
                          bool isFrontFacing);

    // Called from any thread; just returns the handle
    uint64_t getOutputTexture() const;
    void     getOutputDimensions(int* outW, int* outH) const;

    // Set which trivial test shader to run
    CARStatus setTestMode(CARTestMode mode);

    void getStats(CARPhase0Stats* outStats) const;

private:
    void processFrame(uint64_t cameraTexHandle, int w, int h,
                      int rotation, bool isFrontFacing);

    void ensureOutputFramebuffer(int w, int h);
    void ensureShaders();

    std::unique_ptr<RenderContext> ctx_;
    std::unique_ptr<Framebuffer>   outputFbo_;
    int outputWidth_ = 0;
    int outputHeight_ = 0;

    // Test shaders for Phase 0 smoke testing
    std::unique_ptr<ShaderProgram> shaderPassthrough_;
    std::unique_ptr<ShaderProgram> shaderGrayscale_;
    std::unique_ptr<ShaderProgram> shaderInvert_;
    std::unique_ptr<ShaderProgram> shaderVignette_;
    std::atomic<CARTestMode> testMode_{CAR_TEST_MODE_PASSTHROUGH};

    // Diagnostics
    mutable std::mutex statsMutex_;
    float  avgFrameTimeMs_ = 0.0f;
    int    framesProcessed_ = 0;
    int    framesDropped_ = 0;

    // Concurrency: submitFrame can be called from camera thread while
    // getOutputTexture is called from Flutter's render thread.
    mutable std::mutex outputMutex_;
};

}  // namespace community_ar
