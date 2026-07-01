// ============================================================================
// Patch instructions for native/core/perception/face_landmarker.cpp
// ============================================================================
//
// EDIT 1: Add include at the top of the file
// ----------------------------------------------------------------------------
#include "blazeface_decoder.h"

// EDIT 2: Add to FaceLandmarker::Impl members (near `tracker`)
// ----------------------------------------------------------------------------
BlazeFaceConfig                blazeCfg;        // default-initialized = short range
std::vector<BlazeFaceAnchor>   blazeAnchors;
std::vector<float>             detectorScores;  // separate output tensor

// EDIT 3: Add to FaceLandmarker::initialize(), after detectorModel is loaded
// ----------------------------------------------------------------------------
impl_->blazeAnchors = generateBlazeFaceAnchors(impl_->blazeCfg);
// Sanity check — for the short-range config this MUST be 896
if (impl_->blazeAnchors.size() != 896) {
    // Log + return false: config doesn't match the model variant we expect
    return false;
}
impl_->detectorOutput.resize(896 * 16);   // boxes + keypoints
impl_->detectorScores.resize(896);        // separate confidence tensor

// EDIT 4: Replace decodeBlazeFaceOutput call inside FaceLandmarker::run()
// ----------------------------------------------------------------------------
// REPLACE:
//   impl_->detectorOutput.resize(896 * 17);
//   impl_->detectorModel->readOutput(0, impl_->detectorOutput.data(),
//       impl_->detectorOutput.size() * sizeof(float));
//   std::vector<DetectedFace> detections =
//       decodeBlazeFaceOutput(impl_->detectorOutput.data(), 896);
//
// WITH:
impl_->detectorModel->readOutput(0, impl_->detectorOutput.data(),
    impl_->detectorOutput.size() * sizeof(float));
impl_->detectorModel->readOutput(1, impl_->detectorScores.data(),
    impl_->detectorScores.size() * sizeof(float));
std::vector<DetectedFace> detections = decodeBlazeFaceOutput(
    impl_->detectorOutput.data(),
    impl_->detectorScores.data(),
    impl_->blazeAnchors,
    impl_->blazeCfg);

// ============================================================================
// And update CMakeLists.txt to compile blazeface_decoder.cpp:
//   list(APPEND CAR_SOURCES native/core/perception/blazeface_decoder.cpp)
// ============================================================================
