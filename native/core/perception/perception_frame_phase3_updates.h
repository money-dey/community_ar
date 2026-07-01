// perception_frame_phase3_updates.h
// =============================================================================
// Phase 3 additions to PerceptionFrame.
//
// This file documents the new fields that get added to the existing
// PerceptionFrame struct (in perception_frame.h) when Phase 3 lands. It is
// presented as a separate file to keep the changes reviewable; the actual
// canonical perception_frame.h gets these fields merged in.
//
// Backward compatibility (open question Q6 from car-phase-3-requirements.md):
//   The Phase 1 `hairMask` top-level field continues to exist. When the
//   multiclass backend is active, `hairMask` points to the same texture as
//   `segmentationMasks.hair`. When the hair-only backend is active,
//   `hairMask` is populated and the other new fields are nullptr.
//   Existing Phase 1/Phase 2 code accessing `frame.hairMask` keeps working
//   regardless of which backend is in use.
//
// Documented additions:
//
//   PerceptionFrame {
//     ... existing Phase 0/1/2 fields ...
//
//     // === Phase 3 additions ===
//
//     // The full multi-channel segmentation output, populated when the
//     // multiclass segmenter is active. Each field is a shared_ptr<TextureHandle>;
//     // nullptr if the backend doesn't produce that channel.
//     struct SegmentationMasks {
//         std::shared_ptr<TextureHandle> background;
//         std::shared_ptr<TextureHandle> hair;
//         std::shared_ptr<TextureHandle> bodySkin;
//         std::shared_ptr<TextureHandle> faceSkin;
//         std::shared_ptr<TextureHandle> clothes;
//         std::shared_ptr<TextureHandle> others;
//
//         // True if this frame contains fresh segmenter output. Some
//         // backends throttle to every Nth frame for performance.
//         bool fresh = false;
//
//         // Backend name for diagnostics.
//         std::string backendName;
//     } segmentationMasks;
//
//     // === Compatibility aliases ===
//     //
//     // hairMask is now a convenience alias pointing into segmentationMasks.hair
//     // (or holding the Phase 1 hair_segmenter output if that backend is active).
//     // Already exists in PerceptionFrame as a top-level field from Phase 1.
//     // Phase 3 just changes how it's populated:
//     //
//     //   if (multiclass active)  hairMask = segmentationMasks.hair;
//     //   else                    hairMask = (Phase 1 hair-segmenter output);
//     //
//     // Existing call sites do not change.
//   }
//
// Implementation owner: this is plumbing in perception_pipeline.cpp's
// per-frame populate logic. The struct itself is just data.
// =============================================================================

#pragma once

// (Header is documentation-only; actual struct fields are merged into
// perception_frame.h directly. This file exists so the Phase 3 delta is
// reviewable in isolation, matching the convention from Phase 1's
// fix-round delta files.)
