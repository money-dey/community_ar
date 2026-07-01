// perception_pipeline_impl.h
// =============================================================================
// Internal definition of PerceptionPipeline::Impl.
//
// This is a PRIVATE header, included only by the PerceptionPipeline translation
// units (perception_pipeline.cpp and perception_pipeline_phase3_updates.cpp).
// It exists so both files share a single Impl layout — before consolidation the
// per-frame run() logic and the Phase 3 segmenter logic each assumed their own
// (incompatible) set of members, and Impl was never actually defined. All
// pipeline state now lives here and is reached through impl_->.
// =============================================================================

#pragma once

#include "perception_pipeline.h"
#include "perception_frame.h"
#include "face_landmarker.h"
#include "iris_landmarker.h"
#include "skin_tone.h"
#include "segmenter_backend.h"   // brings NeuralBackend + RenderContext complete
#include <memory>
#include <string>
#include <unordered_map>

namespace community_ar {

struct PerceptionPipeline::Impl {
    // ---- Config + wiring (set at construction) ----
    PerceptionConfig config;
    NeuralBackend*   backend = nullptr;
    RenderContext*   ctx     = nullptr;

    // ---- Per-frame state ----
    int64_t          frameId = 0;
    PerceptionFrame  currentFrame;
    PerceptionInputs requirements;

    // ---- Perception sub-models (Phase 1) ----
    std::unique_ptr<FaceLandmarker> faceLandmarker;
    std::unique_ptr<IrisLandmarker> irisLandmarker;

    // Per-track skin tone estimators, keyed by stable faceId. GC'd each frame
    // for faces that didn't appear (retainOnly pattern).
    std::unordered_map<int, std::unique_ptr<SkinToneEstimator>> skinToneByTrack;

    // ---- Segmentation (Phase 3) ----
    // The polymorphic segmenter backend replaces the Phase 1 dedicated
    // HairSegmenter; run() drives it via runSegmenterForFrame(). Created
    // separately by initSegmenterBackend() once model availability is known.
    std::unique_ptr<SegmenterBackend> segmenterBackend;
    bool segmenterNeededThisFrame = false;

    // ---- Diagnostics ----
    struct Diagnostics {
        std::string segmenterBackendName;
        float       lastSegmenterMs = 0.0f;
    } diagnostics;
};

}  // namespace community_ar
