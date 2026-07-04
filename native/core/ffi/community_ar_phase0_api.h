// community_ar_phase0_api.h
// =============================================================================
// Community AR — Phase 0 minimal C ABI
//
// This is a SUBSET of the full C ABI (community_ar_c_api.h), containing only
// what Phase 0 needs to prove the camera→C++→Flutter texture pipeline.
//
// Once Phase 0 is verified working, this gets merged into the full ABI.
// =============================================================================

#ifndef COMMUNITY_AR_PHASE0_API_H
#define COMMUNITY_AR_PHASE0_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
  #define CAR_EXPORT __declspec(dllexport)
#else
  #define CAR_EXPORT __attribute__((visibility("default")))
#endif

typedef struct CARSession CARSession;

typedef enum {
    CAR_BACKEND_GLES  = 1,
    CAR_BACKEND_METAL = 3
} CARBackend;

typedef enum {
    CAR_STATUS_OK               = 0,
    CAR_STATUS_INVALID_HANDLE   = 1,
    CAR_STATUS_INVALID_SESSION  = 2,   /* session pointer null or not started */
    CAR_STATUS_INVALID_ARGUMENT = 3,   /* a call argument was out of range/null */
    CAR_STATUS_INTERNAL_ERROR   = 6
} CARStatus;

typedef enum {
    CAR_TEST_MODE_PASSTHROUGH = 0,  // copy camera to output, no effect
    CAR_TEST_MODE_GRAYSCALE   = 1,  // grayscale shader (smoke test)
    CAR_TEST_MODE_INVERT      = 2,  // color invert (smoke test)
    CAR_TEST_MODE_VIGNETTE    = 3   // vignette (smoke test for spatial work)
} CARTestMode;

typedef struct {
    CARBackend backend;
    // Native handle to platform GPU context:
    //   Android: EGLContext as uint64_t
    //   iOS: MTLDevice* as uint64_t
    uint64_t gpuContext;
    uint64_t gpuDisplay;  // EGLDisplay on Android, unused on iOS
    int logLevel;
} CARPhase0Config;

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------
CAR_EXPORT CARSession* car_p0_create(const CARPhase0Config* cfg, CARStatus* outStatus);
CAR_EXPORT void        car_p0_destroy(CARSession* session);

// -----------------------------------------------------------------------------
// Frame submission — called from platform camera callback
//
// Camera frames arrive as already-on-GPU textures (zero copy). The native
// handle convention:
//   Android: GLuint texture name (OES external texture from SurfaceTexture)
//   iOS:     uint64_t reinterpretation of CVMetalTextureRef
// -----------------------------------------------------------------------------
CAR_EXPORT CARStatus car_p0_submit_frame(CARSession* session,
                                         uint64_t cameraTextureHandle,
                                         int width,
                                         int height,
                                         int rotationDegrees,
                                         int isFrontFacing);

// -----------------------------------------------------------------------------
// Frame submission (display present path) — added for the Android GL/EGL
// bring-up (see docs/ANDROID_RENDER_PIPELINE.md, Option A).
//
// Unlike car_p0_submit_frame — which renders into an offscreen framebuffer
// whose color texture is then handed to Flutter — this variant renders the
// camera texture straight into the *default* framebuffer (fbo 0). Under
// Option A that default framebuffer IS the platform window surface built from
// the Flutter SurfaceTexture, so the caller (the Kotlin/EGL layer that owns
// the window surface) presents the result with eglSwapBuffers afterwards.
//
// `width`/`height` are the display (window-surface) dimensions; native uses
// them for the viewport and reports them via car_p0_get_output_dimensions.
// `texMatrix16` is a column-major 4x4 (android.opengl.Matrix convention)
// applied to the quad's (u, v, 0, 1) in the vertex shader — it folds together
// the SurfaceTexture transform and any rotation/mirror. Pass the 16-float
// identity for no transform. A null pointer is treated as identity.
//
// This is a NEW symbol; car_p0_submit_frame is left unchanged (C ABI stability
// invariant — see CLAUDE.md §2).
// -----------------------------------------------------------------------------
CAR_EXPORT CARStatus car_p0_submit_frame_display(CARSession* session,
                                                 uint64_t cameraTextureHandle,
                                                 int width,
                                                 int height,
                                                 int rotationDegrees,
                                                 int isFrontFacing,
                                                 const float* texMatrix16);

// -----------------------------------------------------------------------------
// Output access — called by platform adapter
//
// Returns the native handle of the most recently produced output texture.
// The platform adapter registers this with Flutter's TextureRegistry.
// Lifetime: stable for the session lifetime.
// -----------------------------------------------------------------------------
CAR_EXPORT uint64_t car_p0_get_output_texture(CARSession* session);
CAR_EXPORT void     car_p0_get_output_dimensions(CARSession* session,
                                                 int* outWidth, int* outHeight);

// -----------------------------------------------------------------------------
// Test mode — switches the trivial test shader for smoke testing
// -----------------------------------------------------------------------------
CAR_EXPORT CARStatus car_p0_set_test_mode(CARSession* session, CARTestMode mode);

// -----------------------------------------------------------------------------
// Model directory (AR integration, docs/AR_INTEGRATION_SPEC.md WP-A).
//
// Tells the session where the .tflite model files live on device (on Android:
// filesDir/models after the plugin's first-launch asset extraction). Call
// after car_p0_create and before the first frame that runs perception — the
// neural backend captures the value when it is lazily created. Callable from
// any thread; the string is copied.
//
// NEW symbol. CARPhase0Config is a shipped struct whose layout is
// stability-bound (CLAUDE.md §2), so the model directory is an additive
// setter rather than a new config field.
// -----------------------------------------------------------------------------
CAR_EXPORT CARStatus car_p0_set_model_directory(CARSession* session,
                                                const char* modelDirectory);

// -----------------------------------------------------------------------------
// Diagnostics
// -----------------------------------------------------------------------------
typedef struct {
    float avgFrameTimeMs;
    int   framesProcessed;
    int   framesDropped;
} CARPhase0Stats;

CAR_EXPORT void car_p0_get_stats(CARSession* session, CARPhase0Stats* outStats);
CAR_EXPORT const char* car_p0_version_string(void);

#ifdef __cplusplus
}
#endif

#endif
