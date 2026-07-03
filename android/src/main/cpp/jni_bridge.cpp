// jni_bridge.cpp
// =============================================================================
// JNI declarations matching CommunityARPlugin.kt's `external fun` signatures.
// Translates JNI calls into the C ABI (community_ar_phase0_api.h).
// Kept thin: no business logic, only argument marshalling.
// =============================================================================

#include <jni.h>
#include <EGL/egl.h>
#include "community_ar_phase0_api.h"

extern "C" {

JNIEXPORT jlong JNICALL
Java_dev_communityar_CommunityARPlugin_nativeCreateSession(
        JNIEnv* env, jobject /*thiz*/, jobject /*flutterSurfaceTexture*/) {
    // Capture the current EGLContext / EGLDisplay that Flutter set up.
    // The C++ side will use these to share textures with Flutter.
    EGLContext eglCtx = eglGetCurrentContext();
    EGLDisplay eglDpy = eglGetCurrentDisplay();

    CARPhase0Config cfg{};
    cfg.backend = CAR_BACKEND_GLES;
    cfg.gpuContext = reinterpret_cast<uint64_t>(eglCtx);
    cfg.gpuDisplay = reinterpret_cast<uint64_t>(eglDpy);
    cfg.logLevel = 3;

    CARStatus st;
    CARSession* s = car_p0_create(&cfg, &st);
    return reinterpret_cast<jlong>(s);
}

JNIEXPORT void JNICALL
Java_dev_communityar_CommunityARPlugin_nativeDestroySession(
        JNIEnv*, jobject, jlong ptr) {
    car_p0_destroy(reinterpret_cast<CARSession*>(ptr));
}

JNIEXPORT void JNICALL
Java_dev_communityar_CommunityARPlugin_nativeSubmitFrame(
        JNIEnv*, jobject, jlong ptr,
        jlong textureHandle, jint width, jint height,
        jint rotation, jint isFront) {
    car_p0_submit_frame(reinterpret_cast<CARSession*>(ptr),
        static_cast<uint64_t>(textureHandle), width, height, rotation, isFront);
}

JNIEXPORT void JNICALL
Java_dev_communityar_CommunityARPlugin_nativeSubmitFrameDisplay(
        JNIEnv* env, jobject, jlong ptr,
        jlong textureHandle, jint width, jint height,
        jint rotation, jint isFront, jfloatArray texMatrix) {
    // Copy the 4x4 UV transform (column-major, android.opengl.Matrix layout)
    // out of the Java float[16]. A null/short array falls back to identity
    // (handled native-side by passing nullptr).
    float mat[16];
    const float* matPtr = nullptr;
    if (texMatrix != nullptr && env->GetArrayLength(texMatrix) >= 16) {
        env->GetFloatArrayRegion(texMatrix, 0, 16, mat);
        matPtr = mat;
    }
    car_p0_submit_frame_display(reinterpret_cast<CARSession*>(ptr),
        static_cast<uint64_t>(textureHandle), width, height, rotation, isFront,
        matPtr);
}

JNIEXPORT void JNICALL
Java_dev_communityar_CommunityARPlugin_nativeSetTestMode(
        JNIEnv*, jobject, jlong ptr, jint mode) {
    car_p0_set_test_mode(reinterpret_cast<CARSession*>(ptr),
                         static_cast<CARTestMode>(mode));
}

JNIEXPORT void JNICALL
Java_dev_communityar_CommunityARPlugin_nativeGetOutputDimensions(
        JNIEnv* env, jobject, jlong ptr, jintArray out) {
    int w = 0, h = 0;
    car_p0_get_output_dimensions(reinterpret_cast<CARSession*>(ptr), &w, &h);
    jint dims[2] = { w, h };
    env->SetIntArrayRegion(out, 0, 2, dims);
}

JNIEXPORT jfloatArray JNICALL
Java_dev_communityar_CommunityARPlugin_nativeGetStats(
        JNIEnv* env, jobject, jlong ptr) {
    CARPhase0Stats stats{};
    car_p0_get_stats(reinterpret_cast<CARSession*>(ptr), &stats);
    jfloatArray arr = env->NewFloatArray(3);
    jfloat values[3] = {
        stats.avgFrameTimeMs,
        static_cast<jfloat>(stats.framesProcessed),
        static_cast<jfloat>(stats.framesDropped)
    };
    env->SetFloatArrayRegion(arr, 0, 3, values);
    return arr;
}

}  // extern "C"
