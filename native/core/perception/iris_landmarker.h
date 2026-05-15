// iris_landmarker.h
// =============================================================================
// Community AR — Iris landmarker (production)
//
// Two-pass-per-frame iris detection: one model invocation per eye, both
// using the same MediaPipe Iris .tflite/.mlmodelc. Each eye's output is
// temporally smoothed via its own One-Euro filter, keyed by the parent
// face's stable trackId (so per-eye state survives face reordering).
//
// Per-track state structure:
//   {faceId} -> {leftFilter, rightFilter}
// Garbage-collected when faces retire (mirroring face_landmarker.cpp).
// =============================================================================

#pragma once

#include "perception_frame.h"
#include "one_euro_filter.h"
#include "../ml/neural_backend.h"
#include <memory>
#include <unordered_map>

namespace community_ar {

class TextureHandle;

class IrisLandmarker {
public:
    explicit IrisLandmarker(NeuralBackend* backend);
    ~IrisLandmarker();

    bool initialize();

    // Updates face.iris in-place. cameraTex is the full camera frame in
    // normalized image coordinates; the crop is computed from face landmarks.
    //
    // `timestampSec` drives the One-Euro filter; `faceId` keys the per-track
    // filter bank.
    bool run(const TextureHandle& cameraTex,
             int imageWidth, int imageHeight,
             float timestampSec,
             FaceData& face);

    // Clear all per-track iris filters (camera switch, session resume).
    void resetTracking();

    // GC: drop filter state for faces that haven't appeared recently.
    void retainOnly(const std::vector<int>& activeFaceIds);

    void setFilterParams(float minCutoff, float beta, float dCutoff);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace community_ar
