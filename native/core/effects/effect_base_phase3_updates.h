// effect_base_phase3_updates.h
// =============================================================================
// Phase 3 additions to the Effect base interface.
//
// This file documents the new types and virtual methods being added to
// effect_base.h. As with other Phase N update files in this project, the
// actual canonical effect_base.h gets these merged in. This file exists
// so the deltas are reviewable in isolation.
//
// Backward compatibility:
//   All new virtuals have non-pure defaults that produce Phase 2 behavior.
//   Existing Phase 2 effects (LipsEffect / MaskedRecolorEffect) need no
//   code changes to compile against the new interface.
//
// Conceptual additions:
//
//   // ===== New namespace-scope type =====
//   //
//   // Mask resources an effect reads and/or writes. Parallel to
//   // PerceptionInputs (which describes what an effect needs from the
//   // perception layer); this struct describes what it needs from the
//   // mask pool.
//   //
//   // - consumes: names of masks the effect needs available before its
//   //             prepare() runs (the graph guarantees these are populated
//   //             from either segmenter output or earlier producers)
//   // - produces: names of masks this effect makes available for later
//   //             effects. Rare — most effects only consume.
//   //
//   // Defined at namespace scope (not nested in Effect) for symmetry
//   // with PerceptionInputs and to keep call sites readable:
//   //   MaskRequirements req;            // works
//   //   Effect::MaskRequirements req;    // would have been required if nested
//   struct MaskRequirements {
//       std::vector<std::string> consumes;
//       std::vector<std::string> produces;
//   };
//
//   // ===== Additions to class Effect =====
//
//   class Effect {
//   public:
//     // ... existing Phase 2 methods (typeId, perceptionInputs, prepare,
//     //                                 render) unchanged ...
//
//     // Pass-order category for graph sorting. The graph runs effects in
//     // ascending order of this value, stable within the same value.
//     // Default: Recolor (matches Phase 2 effects' pipeline position).
//     virtual EffectPass passOrder() const { return EffectPass::Recolor; }
//
//     // Mask resources this effect reads and/or writes.
//     // Default: empty for both (Phase 2 effects rasterize their own masks
//     // internally and don't share them via the pool).
//     virtual MaskRequirements maskRequirements() const { return {}; }
//
//     // Phase 3 overload of prepare() that receives the mask pool.
//     // The graph calls the new overload by default; the old overload
//     // remains for compatibility with effects that don't need pool access.
//     //
//     // Default implementation delegates to the Phase 2 prepare() so
//     // existing effects keep working without changes.
//     virtual void prepare(const PerceptionFrame& frame,
//                          MaskResourcePool& maskPool,
//                          RenderContext* ctx) {
//       (void)maskPool;
//       prepare(frame, ctx);
//     }
//
//     // Phase 3 overload of render() that receives the mask pool.
//     // Same delegation pattern as prepare().
//     virtual void render(const TextureHandle& inputTex,
//                         Framebuffer* outputFbo,
//                         const MaskResourcePool& maskPool,
//                         RenderContext* ctx) {
//       (void)maskPool;
//       render(inputTex, outputFbo, ctx);
//     }
//   };
//
// Implementation owner: small surgical edit to effect_base.h.
//   - Add the namespace-scope MaskRequirements struct above the class.
//   - Add the four new virtuals to Effect.
//   - No changes to Phase 2 effects required.
//
// New Phase 3 effects (SkinSmoothEffect specifically) override the
// pool-receiving versions to declare and consume mask resources.
// =============================================================================

#pragma once

// (Documentation-only; canonical declaration merges into effect_base.h.)
