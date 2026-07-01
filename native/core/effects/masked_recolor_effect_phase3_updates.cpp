// masked_recolor_effect_phase3_updates.cpp
// =============================================================================
// Phase 3 surgical update to MaskedRecolorEffect.
//
// What changes:
//   - Add an explicit passOrder() override returning EffectPass::Recolor.
//     This makes the effect's pipeline position visible in code rather
//     than relying on the base class default.
//   - No other code changes. The effect continues to rasterize its own
//     mask internally rather than going through the MaskResourcePool.
//
// What deliberately does NOT change yet:
//   - The lip mask rasterized in prepare() is private to the effect, not
//     shared via the pool. In Phase 6, when we add IrisEffect/TeethEffect
//     /BrowsEffect that could each share a face-region mask pipeline, we
//     reconsider — likely by adding `produces: ["masks.lipsContour"]` so a
//     hypothetical UnderEyeEffect could reuse parts of the mask production.
//   - For Phase 3 with a single Bucket A effect type (Lips) that has a
//     unique landmark contour, there's nothing to share. Premature
//     refactoring would add complexity without benefit.
//
// What the delta looks like in the canonical header:
//
//   class MaskedRecolorEffect : public Effect {
//   public:
//     // ... existing Phase 2 declarations ...
//
//     // Phase 3 addition:
//     EffectPass passOrder() const override { return EffectPass::Recolor; }
//
//     // (We do NOT override maskRequirements() yet; the default empty
//     // requirements are correct for now.)
//   };
//
// The behavior of prepare() and render() does NOT change. The base class's
// default Phase 3 overloads delegate to the Phase 2 signatures, so the
// effect's internal logic is untouched.
// =============================================================================

// (Documentation-only — see file header for the precise edit.)
