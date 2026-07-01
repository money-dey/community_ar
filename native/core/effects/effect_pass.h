// effect_pass.h
// =============================================================================
// Community AR — EffectPass enum
//
// Stable categorical ordering for effects in the graph. The EffectGraph
// sorts effects by passOrder() before running them, so an effect's logical
// stage in the pipeline does not depend on the declaration order in user
// code.
//
// Why this matters concretely:
//   - SkinSmoothEffect must run BEFORE LipsEffect. Otherwise lipstick gets
//     sampled from raw camera skin and then has the beautify pass over it,
//     producing weird softness on the lip outline.
//   - Future AssetOverlay effects (glasses) must run AFTER recolor effects.
//     Otherwise the lipstick would tint the glasses frame where they cross
//     the lips.
//   - PostProcess (grain, vignette) is always last by definition.
//
// Gap-numbered values:
//   The integer gaps between named values reserve room to add new pass
//   categories without renumbering. If a Phase 7 effect needs to land
//   between SkinAdjust and Recolor, we can pick e.g. 150 without disturbing
//   the named constants. Stability for the public ABI.
//
// Within a pass:
//   Effects with the same passOrder() value run in declaration order
//   (stable sort). For users who care about ordering of equal-priority
//   effects (rare — two lipstick effects?), the rule is "the order you
//   pass them to EffectGraph is the order they execute."
// =============================================================================

#pragma once

#include <cstdint>

namespace community_ar {

enum class EffectPass : uint32_t {
    // Base color grading — LUT-based looks, runs first so all effects above
    // sample the graded base.
    BaseColorGrade  = 0,    // Phase 5.5: ImageGradeEffect

    // Skin-area modifications — beautification, skin retouching, etc.
    // Must run before recolor because recolor effects sample skin colors
    // and want the beautified version.
    SkinAdjust      = 100,  // Phase 3: SkinSmoothEffect (beauty v2)

    // Recolor effects — lips, iris, teeth, brows, hair color, etc.
    // Most user-facing "makeup" effects live here.
    Recolor         = 200,  // Phase 2-6: MaskedRecolorEffect family

    // Geometric warp effects — eye enlarge, nose reshape, lip plump.
    // Must come after recolor because the warp samples pre-warped pixels
    // (including any recolored regions).
    Warp            = 300,  // Phase 4: LandmarkWarpEffect family

    // 3D asset overlay — glasses, hats, earrings, 3D hair. Composited on
    // top of all 2D effects because the assets sit physically in front
    // of the face.
    Overlay         = 400,  // Phase 5: AssetOverlayEffect family

    // Background effects — blur, replace. Runs after the foreground is
    // finished so we know what to replace.
    Background      = 450,  // Phase 6: BackgroundEffect

    // Post-process — grain, vignette, light leaks. Always last; affects
    // the entire image including overlays.
    PostProcess     = 500,  // Phase 5.5: FilmGrain, Vignette, etc.
};

// Numeric value extractor for sort/comparison
inline uint32_t passOrderValue(EffectPass p) {
    return static_cast<uint32_t>(p);
}

}  // namespace community_ar
