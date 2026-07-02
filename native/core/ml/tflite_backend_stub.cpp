// tflite_backend_stub.cpp
// =============================================================================
// Stub TensorFlow Lite backend.
//
// Compiled INSTEAD of tflite_backend.cpp when TFLite is not vendored (see the
// optional-TFLite section of the root CMakeLists.txt). It provides
// createTfliteBackend() returning nullptr so the native library still links and
// the Phase 0 camera pipeline runs.
//
// On-device perception (landmarks, iris, segmentation, skin tone, and the
// beauty pipeline that depends on it) stays inactive until TFLite is vendored:
//   bash tools/fetch_tflite.sh    (+ the from-source library build)
//   see third_party/tensorflow-lite/README.md
// =============================================================================

#if defined(__ANDROID__)

#include "neural_backend.h"
#include <android/log.h>
#include <memory>

namespace community_ar {

std::unique_ptr<NeuralBackend> createTfliteBackend(const BackendConfig&) {
    __android_log_print(ANDROID_LOG_WARN, "CommunityAR",
        "TensorFlow Lite is not compiled in; perception is disabled. "
        "Vendor TFLite (tools/fetch_tflite.sh) and rebuild to enable it.");
    return nullptr;
}

// Per-frame ML input-blit-cache reset — a no-op without a real TFLite backend.
// Matches the extern "C" declaration in perception_pipeline.cpp.
extern "C" void tflite_backend_invalidate_frame(NeuralBackend*) {}

}  // namespace community_ar

#endif  // __ANDROID__
