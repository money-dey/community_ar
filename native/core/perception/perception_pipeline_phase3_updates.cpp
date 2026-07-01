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
#include "perception_pipeline_impl.h"
#include "segmenter_backend.h"
#include "segmenter_backend_factory.h"

namespace community_ar {

// All Phase 3 segmenter state (segmenterBackend, segmenterNeededThisFrame,
// diagnostics) now lives in PerceptionPipeline::Impl — see
// perception_pipeline_impl.h — and is reached through impl_->, the same as the
// Phase 1 per-frame state in perception_pipeline.cpp.

// -----------------------------------------------------------------------------
// Initialization (called from the session creation path)
// -----------------------------------------------------------------------------
void PerceptionPipeline::initSegmenterBackend(
        NeuralBackend* neuralBackend,
        const SegmenterBackendConfig& cfg) {
    impl_->segmenterBackend = createSegmenterBackend(neuralBackend, cfg);
    if (impl_->segmenterBackend) {
        impl_->diagnostics.segmenterBackendName = impl_->segmenterBackend->name();
    } else {
        impl_->diagnostics.segmenterBackendName = "none";
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
    if (!impl_->segmenterBackend || !impl_->segmenterNeededThisFrame) {
        // No segmenter or no effect needs masks this frame.
        // Leave segmentationMasks default-constructed (all nullptr, fresh=false).
        return;
    }

    SegmentationChannels channels = impl_->segmenterBackend->run(cameraTex, ctx);

    // Populate the structured segmentationMasks field
    outFrame.segmentationMasks.background  = channels.background;
    outFrame.segmentationMasks.hair        = channels.hair;
    outFrame.segmentationMasks.bodySkin    = channels.bodySkin;
    outFrame.segmentationMasks.faceSkin    = channels.faceSkin;
    outFrame.segmentationMasks.clothes     = channels.clothes;
    outFrame.segmentationMasks.others      = channels.others;
    outFrame.segmentationMasks.fresh       = channels.fresh;
    outFrame.segmentationMasks.backendName = channels.backendName;

    // Keep the backward-compatible raw hairMask pointer populated (what Phase
    // 1/2 code reads). The texture is owned by segmentationMasks.hair (and the
    // segmenter backend), so this borrowed pointer stays valid until next run().
    outFrame.hairMask = channels.hair.get();

    // Diagnostic — what's the segmenter inference cost this frame?
    impl_->diagnostics.lastSegmenterMs = impl_->segmenterBackend->lastInferenceMs();
}

// -----------------------------------------------------------------------------
// setRequirements — Phase 3 changes
//
// When EffectGraph hands us the union of all effects' inputs, we record
// whether the segmenter is needed this frame.
// -----------------------------------------------------------------------------
void PerceptionPipeline::setRequirements(const PerceptionInputs& in) {
    // Record the union of effect requirements (read per-frame by run()).
    impl_->requirements = in;

    // Phase 3 logic: does ANY effect need a mask the segmenter produces?
    impl_->segmenterNeededThisFrame =
        in.needsHairMask        ||
        in.needsFaceSkinMask    ||
        in.needsBodySkinMask    ||
        in.needsClothesMask     ||
        in.needsBackgroundMask;

    // If the active backend doesn't produce a needed channel, effects that
    // need it fall back to a landmark-derived mask (handled downstream). The
    // check is kept here as the natural place to surface a diagnostic later.
    if (impl_->segmenterBackend) {
        auto produced = impl_->segmenterBackend->channelsProduced();
        (void)produced;
    }
}

}  // namespace community_ar
