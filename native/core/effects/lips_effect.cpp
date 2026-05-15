// lips_effect.cpp
// =============================================================================
// Community AR — LipsEffect factory
//
// LipsEffect is not a class. It's a factory function that returns a
// MaskedRecolorEffect pre-configured with:
//   - The MediaPipe FaceMesh outer-lip contour indices (additive)
//   - The MediaPipe FaceMesh inner-lip contour indices (subtractive when
//     jawOpen blendshape > threshold)
//   - The user's color and opacity / softness / luminance-preserve params
//
// Every Bucket A effect added in Phase 6 (IrisEffect, TeethEffect, etc.)
// follows the exact same pattern. The factory body is the only
// effect-specific code that needs to exist.
// =============================================================================

#include "masked_recolor_effect.h"
#include "effect_types.h"
#include "../ffi/community_ar_phase2_api.h"
#include <memory>

namespace community_ar {

// -----------------------------------------------------------------------------
// MediaPipe FaceMesh — Lip contour indices
//
// Indices are for the 468-point FaceMesh model and walk the contour in
// clockwise order. The polygon closes by connecting the last point back
// to the first. (We do NOT need to repeat the first index at the end —
// the triangle fan rasterizer wraps automatically.)
//
// Source: MediaPipe face_geometry/data/canonical_face_model_uv_visualization.png
// and double-checked against face_landmarker_constants in the official repo.
// -----------------------------------------------------------------------------
static const std::vector<int> kLipsOuterContour = {
    // Upper lip (right corner → top → left corner)
    61, 185, 40, 39, 37, 0, 267, 269, 270, 409, 291,
    // Lower lip (left corner → bottom → back to right corner)
    375, 321, 405, 314, 17, 84, 181, 91, 146
};

static const std::vector<int> kLipsInnerContour = {
    // Inner upper lip
    78, 191, 80, 81, 82, 13, 312, 311, 310, 415, 308,
    // Inner lower lip
    324, 318, 402, 317, 14, 87, 178, 88, 95
};

// jawOpen is ARKit blendshape index 25 (standard ordering used by MediaPipe
// FaceLandmarker's blendshape output). Threshold of 0.05 reliably detects
// any mouth opening beyond passive closure, including subtle ones during
// speech.
static constexpr int   kJawOpenBlendshapeIndex = 25;
static constexpr float kJawOpenThreshold       = 0.05f;

// -----------------------------------------------------------------------------
// makeLipsEffect — the factory
//
// Called by car_p2_graph_set() when it sees CAR_EFFECT_LIPS in the effect
// type ID list. Receives the raw config bytes from Dart, casts to the
// struct, validates the version, builds a MaskedRecolorEffect::Config.
// -----------------------------------------------------------------------------
std::unique_ptr<Effect> makeLipsEffect(const void* configBytes,
                                       size_t configSize) {
    if (configSize < sizeof(CARLipsEffectConfig)) return nullptr;
    const auto* cfg = static_cast<const CARLipsEffectConfig*>(configBytes);
    if (cfg->version != 1) return nullptr;   // unknown version

    MaskedRecolorEffect::Config mc;
    mc.typeId               = CAR_EFFECT_LIPS;
    mc.outerContourIndices  = kLipsOuterContour;
    mc.innerContourIndices  = kLipsInnerContour;
    mc.innerSubtractBlendshape = kJawOpenBlendshapeIndex;
    mc.innerSubtractThreshold  = kJawOpenThreshold;
    mc.colorR              = cfg->colorR;
    mc.colorG              = cfg->colorG;
    mc.colorB              = cfg->colorB;
    mc.opacity             = cfg->opacity;
    mc.edgeSoftness        = cfg->edgeSoftness;
    mc.luminancePreserve   = cfg->luminancePreserve;

    return std::make_unique<MaskedRecolorEffect>(std::move(mc));
}

}  // namespace community_ar
