// face_landmarker.h
// =============================================================================
// Community AR — Face landmarker (production)
//
// Production multi-face pipeline:
//   1. Face detector (BlazeFace) outputs N bounding boxes per frame
//   2. FaceTracker assigns stable IDs via Hungarian IoU
//   3. For each tracked face, run FaceMesh on its cropped region
//   4. Apply per-track One-Euro filters keyed by stable ID
//   5. Output FaceData[] with correct, persistent identity
//
// Per-track state (filters, motion estimate, last-seen landmarks) lives in
// a hashmap keyed by track ID. State is purged when tracks retire.
// =============================================================================

#pragma once

#include "perception_frame.h"
#include "face_tracker.h"
#include "one_euro_filter.h"
#include "../ml/neural_backend.h"
#include <memory>
#include <unordered_map>

namespace community_ar {

class TextureHandle;

class FaceLandmarker {
public:
    FaceLandmarker(NeuralBackend* backend, int maxFaces);
    ~FaceLandmarker();

    bool initialize();

    // Run per-frame perception. Populates `outFaces` with one entry per
    // detected face, with `faceId` set to the stable tracked ID.
    bool run(const TextureHandle& cameraTex,
             int imageWidth, int imageHeight,
             float timestampSec,
             std::vector<FaceData>& outFaces);

    // Reset all per-track state (camera switch, session resume).
    void resetTracking();

    void setFilterParams(float minCutoff, float beta, float dCutoff);

    // Diagnostics
    int activeTrackCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace community_ar
