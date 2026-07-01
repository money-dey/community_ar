// perception_inputs_phase3_updates.h
// =============================================================================
// Phase 3 additions to PerceptionInputs.
//
// The PerceptionInputs struct (originally in perception_pipeline.h) gets
// four new bool flags so effects can declare exactly which segmenter
// channels they need. The perception pipeline ORs them together via
// EffectGraph::perceptionInputs() and configures the segmenter accordingly.
//
// Canonical additions:
//
//   struct PerceptionInputs {
//     // === Existing Phase 1/2 fields ===
//     bool needsFaceLandmarks = false;
//     bool needsIrisLandmarks = false;
//     bool needsHairMask      = false;
//     bool needsSelfieMask    = false;
//     bool needsFacePose      = false;
//     bool needsSkinTone      = false;
//
//     // === Phase 3 additions ===
//
//     // Beauty v2 consumes this. The multiclass segmenter populates
//     // segmentationMasks.faceSkin which lands in maskPool[kFaceSkin].
//     bool needsFaceSkinMask     = false;
//
//     // Future use: background effects that need to know skin-vs-clothing.
//     // No effect consumes this in Phase 3 itself; flag is plumbed now
//     // so adding background effects in Phase 6 is a clean addition.
//     bool needsBodySkinMask     = false;
//
//     // Same as bodySkin — Phase 6 background / clothing effects.
//     bool needsClothesMask      = false;
//
//     // Same — Phase 6 background replacement.
//     bool needsBackgroundMask   = false;
//   };
//
// Note that `needsHairMask` already exists from Phase 1 and continues to
// work. When the multiclass segmenter is active, hair comes from the same
// model as faceSkin; when the hair-only segmenter is active, only
// needsHairMask triggers inference. The pipeline logic in
// perception_pipeline_phase3_updates.cpp handles the unified gating.
// =============================================================================

#pragma once

// (Documentation-only — see file header for the precise edit.)
