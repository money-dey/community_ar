// perception_pipeline_phase3_updates.cpp
// =============================================================================
// Phase 3 updates to PerceptionPipeline.
//
// Replaces the direct Phase 1 HairSegmenter member with a polymorphic
// SegmenterBackend pointer chosen at construction. Per-frame, the pipeline
// calls segmenterBackend_->run() and copies the resulting channels into
// the PerceptionFrame's segmentationMasks struct.
//
// Backward compatibility: the top-level `frame.hairMask` field continues
// to be populated identically. Downstream Phase 1/2 code accessing it
// doesn't change.
//
// As with Phase 1's fix-round files and Phase 2's session-updates file,
// this is a delta document. The actual perception_pipeline.cpp gets these
// changes merged in.
// =============================================================================

#include "perception_pipeline.h"
#include "segmenter_backend.h"
#include "segmenter_backend_factory.h"

namespace community_ar {

// -----------------------------------------------------------------------------
// PerceptionPipeline construction — Phase 3 changes
//
// The constructor now takes a SegmenterBackendConfig in addition to the
// existing PerceptionConfig and NeuralBackend. Default-constructing the
// segmenter config yields "prefer multiclass with fallback" — the
// recommended setting for Phase 3 and forward.
// -----------------------------------------------------------------------------
struct PerceptionPipelinePhase3Members {
    // Replaces the Phase 1 HairSegmenter* member
    std::unique_ptr<SegmenterBackend> segmenterBackend;

    // Track whether segmenter inference is needed this frame.
    // The current PerceptionInputs union (Phase 1's `needsHairMask`,
    // now joined by `needsFaceSkinMask`, `needsBodySkinMask`, etc.)
    // determines if we run inference or skip.
    bool segmenterNeededThisFrame = false;
};

// -----------------------------------------------------------------------------
// Initialization (called from Phase 0 session creation path)
// -----------------------------------------------------------------------------
void PerceptionPipeline::initSegmenterBackend(
        NeuralBackend* neuralBackend,
        const SegmenterBackendConfig& cfg) {
    p3_->segmenterBackend = createSegmenterBackend(neuralBackend, cfg);
    if (p3_->segmenterBackend) {
        diagnostics_.segmenterBackendName = p3_->segmenterBackend->name();
    } else {
        diagnostics_.segmenterBackendName = "none";
    }
}

// -----------------------------------------------------------------------------
// PerceptionInputs — Phase 3 additions
//
// The union of effect requirements now includes the new mask channels.
// EffectGraph::perceptionInputs() ORs these together; the pipeline reads
// the union and decides whether to run the segmenter at all this frame.
// -----------------------------------------------------------------------------
//   struct PerceptionInputs {
//     bool needsFaceLandmarks = false;
//     bool needsIrisLandmarks = false;
//     bool needsHairMask      = false;   // existing
//     bool needsSelfieMask    = false;   // existing
//     bool needsFacePose      = false;
//     bool needsSkinTone      = false;
//
//     // Phase 3 additions:
//     bool needsFaceSkinMask  = false;   // beauty v2 consumes this
//     bool needsBodySkinMask  = false;   // background effects later
//     bool needsClothesMask   = false;   // future effects
//     bool needsBackgroundMask = false;  // future effects
//   };
//
// Logic:
//   segmenterNeeded = needsHairMask || needsFaceSkinMask
//                  || needsBodySkinMask || needsClothesMask
//                  || needsBackgroundMask;
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Per-frame segmenter execution and PerceptionFrame population
// -----------------------------------------------------------------------------
void PerceptionPipeline::runSegmenterForFrame(const TextureHandle& cameraTex,
                                              PerceptionFrame& outFrame,
                                              RenderContext* ctx) {
    if (!p3_->segmenterBackend || !p3_->segmenterNeededThisFrame) {
        // No segmenter or no effect needs masks this frame.
        // Leave segmentationMasks default-constructed (all nullptr, fresh=false).
        return;
    }

    SegmentationChannels channels = p3_->segmenterBackend->run(cameraTex, ctx);

    // Populate the structured segmentationMasks field
    outFrame.segmentationMasks.background  = channels.background;
    outFrame.segmentationMasks.hair        = channels.hair;
    outFrame.segmentationMasks.bodySkin    = channels.bodySkin;
    outFrame.segmentationMasks.faceSkin    = channels.faceSkin;
    outFrame.segmentationMasks.clothes     = channels.clothes;
    outFrame.segmentationMasks.others      = channels.others;
    outFrame.segmentationMasks.fresh       = channels.fresh;
    outFrame.segmentationMasks.backendName = channels.backendName;

    // Keep backward-compatible hairMask field populated. This is what
    // existing Phase 1/Phase 2 code accesses.
    outFrame.hairMask = channels.hair;

    // Diagnostic — what's the segmenter inference cost this frame?
    diagnostics_.lastSegmenterMs = p3_->segmenterBackend->lastInferenceMs();
}

// -----------------------------------------------------------------------------
// setRequirements — Phase 3 changes
//
// When EffectGraph hands us the union of all effects' inputs, we record
// whether the segmenter is needed this frame.
// -----------------------------------------------------------------------------
void PerceptionPipeline::setRequirements(const PerceptionInputs& in) {
    // Existing Phase 1 logic remains:
    requirements_ = in;

    // Phase 3 logic: does ANY effect need a mask the segmenter produces?
    p3_->segmenterNeededThisFrame =
        in.needsHairMask        ||
        in.needsFaceSkinMask    ||
        in.needsBodySkinMask    ||
        in.needsClothesMask     ||
        in.needsBackgroundMask;

    // If the active backend doesn't produce a needed channel, log a warning
    // (once per requirements change, not per frame).
    if (p3_->segmenterBackend) {
        auto produced = p3_->segmenterBackend->channelsProduced();
        if (in.needsFaceSkinMask && !produced.faceSkin) {
            // Hair-only backend can't produce face skin mask.
            // Effects that need it will fall back to landmark-derived mask.
        }
    }
}

}  // namespace community_ar
