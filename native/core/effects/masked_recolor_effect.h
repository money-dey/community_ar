// masked_recolor_effect.h
// =============================================================================
// Community AR — MaskedRecolorEffect (Bucket A engine)
//
// The single engine that powers all 8 future recolor effects:
//   LipsEffect, IrisEffect, TeethEffect, BrowsEffect, UnderEyeEffect,
//   HairThickenEffect, BeardThickenEffect, (SkinSmoothEffect uses a
//   different engine because of multi-band frequency separation).
//
// Each user-facing effect is just a factory function that pre-fills the
// Config with the right landmark contour indices and forwards the user
// parameters. No subclassing.
//
// The engine:
//   1. In prepare(): pulls landmarks from PerceptionFrame, builds
//      MaskContour list (outer additive, inner subtractive if mouth/eye open),
//      hands to MaskRasterizer.
//   2. In render(): one full-screen shader pass reading from inputTex and
//      the mask, blending in Oklab space, writing to outputFbo.
// =============================================================================

#pragma once

#include "effect_base.h"
#include "mask_rasterizer.h"
#include <memory>
#include <vector>

namespace community_ar {

class ShaderProgram;

class MaskedRecolorEffect : public Effect {
public:
    struct Config {
        uint32_t typeId;   // set by the factory (1 = Lips, 2 = Iris, etc.)

        // Per-face landmark indices that form the outer contour. Required.
        std::vector<int> outerContourIndices;

        // Per-face landmark indices that form the inner (subtracted) contour.
        // Empty = no subtraction. Used by lips (don't paint teeth/tongue when
        // mouth is open) and potentially iris (subtract pupil if modeled).
        std::vector<int> innerContourIndices;

        // When to apply inner-contour subtraction.
        //   - innerSubtractBlendshape >= 0: subtract when
        //     face.blendShapes[idx] > threshold
        //   - innerSubtractBlendshape == -1: always subtract
        // For lips: blendshape 25 (jawOpen), threshold 0.05.
        int   innerSubtractBlendshape = -1;
        float innerSubtractThreshold  = 0.0f;

        // Recolor parameters from the user-facing config
        float colorR = 1.0f;
        float colorG = 0.0f;
        float colorB = 0.0f;
        float opacity = 0.85f;
        float edgeSoftness = 0.4f;
        float luminancePreserve = 1.0f;
    };

    explicit MaskedRecolorEffect(Config cfg);
    ~MaskedRecolorEffect() override;

    uint32_t typeId() const override { return cfg_.typeId; }
    PerceptionInputs perceptionInputs() const override;
    void prepare(const PerceptionFrame& frame, RenderContext* ctx) override;
    void render(const TextureHandle& inputTex,
                Framebuffer* outputFbo,
                RenderContext* ctx) override;

private:
    void ensureShader(RenderContext* ctx);
    void collectContours(const PerceptionFrame& frame,
                         std::vector<MaskContour>& out) const;

    Config cfg_;
    std::unique_ptr<MaskRasterizer> rasterizer_;
    std::unique_ptr<ShaderProgram>  recolorShader_;

    int lastOutputWidth_  = 0;
    int lastOutputHeight_ = 0;
};

}  // namespace community_ar
