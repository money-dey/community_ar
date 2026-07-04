// masked_recolor_effect.cpp
// =============================================================================
// MaskedRecolorEffect implementation.
//
// The render path: one full-screen quad with a fragment shader that:
//   1. Samples the input frame and the mask
//   2. Converts both input pixel and target color to Oklab
//   3. Blends per the user's opacity / luminancePreserve parameters
//   4. Converts back to sRGB and writes
//
// Why Oklab specifically:
//   Naive RGB blending makes lipstick on dark skin read as muddy gray.
//   Oklab's (a, b) chromatic axes are perceptually uniform, so the same
//   numeric blend produces visually equivalent shifts on any base color.
//   This matters because most beauty AR apps fail spectacularly on
//   diverse skin tones — exactly what Community AR exists to address.
// =============================================================================

#include "masked_recolor_effect.h"
#include "../render/render_context.h"
#include <cstring>

namespace community_ar {

// -----------------------------------------------------------------------------
// Shader source — Oklab recolor, ~50 lines
// -----------------------------------------------------------------------------
static const char* kRecolorVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)";

static const char* kRecolorFS = R"GLSL(#version 300 es
precision mediump float;

uniform sampler2D uInputFrame;
uniform sampler2D uMask;
uniform vec3      uTargetColor;        // sRGB, [0, 1]
uniform float     uOpacity;
uniform float     uLuminancePreserve;

in  vec2 vUv;
out vec4 fragColor;

// -- sRGB <-> linear --
vec3 srgbToLinear(vec3 c) {
    bvec3 cutoff = lessThan(c, vec3(0.04045));
    vec3 low = c / 12.92;
    vec3 high = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, vec3(cutoff));
}
vec3 linearToSrgb(vec3 c) {
    bvec3 cutoff = lessThan(c, vec3(0.0031308));
    vec3 low = c * 12.92;
    vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, vec3(cutoff));
}

// -- linear sRGB <-> Oklab (Björn Ottosson's formulation) --
// Reference: https://bottosson.github.io/posts/oklab/
vec3 linearToOklab(vec3 c) {
    float l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
    float m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
    float s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;
    float l_ = pow(max(l, 0.0), 1.0 / 3.0);
    float m_ = pow(max(m, 0.0), 1.0 / 3.0);
    float s_ = pow(max(s, 0.0), 1.0 / 3.0);
    return vec3(
        0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
        1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
        0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_
    );
}
vec3 oklabToLinear(vec3 c) {
    float l_ = c.x + 0.3963377774 * c.y + 0.2158037573 * c.z;
    float m_ = c.x - 0.1055613458 * c.y - 0.0638541728 * c.z;
    float s_ = c.x - 0.0894841775 * c.y - 1.2914855480 * c.z;
    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;
    return vec3(
        4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
       -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
       -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s
    );
}

void main() {
    // NOTE: `input` is a RESERVED keyword in GLSL ES — permissive drivers
    // accept it as an identifier, strict ones reject the whole shader
    // (seen on-device: "L0003: Keyword 'input' is reserved"), killing the
    // recolor pass. Hence `srcColor`.
    vec4 srcColor = texture(uInputFrame, vUv);
    float mask  = texture(uMask, vUv).r;
    float a     = mask * uOpacity;

    // Fast path: no mask coverage = passthrough (avoid pow() calls)
    if (a < 0.001) { fragColor = srcColor; return; }

    // Convert source and target to Oklab
    vec3 srcLin = srgbToLinear(srcColor.rgb);
    vec3 tgtLin = srgbToLinear(uTargetColor);
    vec3 srcLab = linearToOklab(srcLin);
    vec3 tgtLab = linearToOklab(tgtLin);

    // Blend in Oklab:
    //   L: preserve source L by uLuminancePreserve fraction.
    //      LuminancePreserve = 1.0 → keeps original brightness (natural)
    //      LuminancePreserve = 0.0 → fully adopts target L (flat painted look)
    //      The mask amount `a` modulates how strong the L shift is too.
    //   a, b: shift toward target by mask amount `a`.
    float L  = mix(srcLab.x, mix(tgtLab.x, srcLab.x, uLuminancePreserve), a);
    float aC = mix(srcLab.y, tgtLab.y, a);
    float bC = mix(srcLab.z, tgtLab.z, a);

    vec3 outLin = oklabToLinear(vec3(L, aC, bC));
    // Clamp to [0,1] before sRGB conversion (Oklab can produce out-of-gamut colors)
    outLin = clamp(outLin, vec3(0.0), vec3(1.0));
    fragColor = vec4(linearToSrgb(outLin), srcColor.a);
}
)GLSL";

// -----------------------------------------------------------------------------
// MaskedRecolorEffect
// -----------------------------------------------------------------------------
MaskedRecolorEffect::MaskedRecolorEffect(Config cfg) : cfg_(std::move(cfg)) {}
MaskedRecolorEffect::~MaskedRecolorEffect() = default;

PerceptionInputs MaskedRecolorEffect::perceptionInputs() const {
    PerceptionInputs in;
    in.needsFaceLandmarks = true;
    // Bucket A effects that use blendshape gating (lip's jawOpen, future
    // eye effects' eyeBlink) get them as a free byproduct of FaceLandmarker.
    // Iris-specific recolor would set in.needsIrisLandmarks = true via a
    // dedicated factory.
    return in;
}

void MaskedRecolorEffect::ensureShader(RenderContext* ctx) {
    if (!recolorShader_) {
        recolorShader_ = ctx->createShader(kRecolorVS, kRecolorFS);
    }
    if (!rasterizer_) {
        rasterizer_ = std::make_unique<MaskRasterizer>(ctx);
    }
}

void MaskedRecolorEffect::collectContours(const PerceptionFrame& frame,
                                          std::vector<MaskContour>& out) const {
    out.clear();
    if (frame.faces.empty()) return;

    out.reserve(frame.faces.size() * 2);   // outer + maybe inner per face

    for (const auto& face : frame.faces) {
        // ---- Outer contour (additive) ----
        MaskContour outer;
        outer.additive = true;
        outer.points.reserve(cfg_.outerContourIndices.size());
        for (int idx : cfg_.outerContourIndices) {
            if (idx >= 0 && idx < FaceLandmarks::kCount) {
                outer.points.push_back(face.landmarks.points[idx]);
            }
        }
        if (outer.points.size() >= 3) out.push_back(std::move(outer));

        // ---- Inner contour (subtractive), gated by blendshape ----
        bool subtractInner = !cfg_.innerContourIndices.empty();
        if (subtractInner && cfg_.innerSubtractBlendshape >= 0) {
            int bi = cfg_.innerSubtractBlendshape;
            if (bi < 52) {
                subtractInner = face.blendShapes[bi] > cfg_.innerSubtractThreshold;
            }
        }
        if (subtractInner) {
            MaskContour inner;
            inner.additive = false;
            inner.points.reserve(cfg_.innerContourIndices.size());
            for (int idx : cfg_.innerContourIndices) {
                if (idx >= 0 && idx < FaceLandmarks::kCount) {
                    inner.points.push_back(face.landmarks.points[idx]);
                }
            }
            if (inner.points.size() >= 3) out.push_back(std::move(inner));
        }
    }
}

void MaskedRecolorEffect::prepare(const PerceptionFrame& frame,
                                  RenderContext* ctx) {
    ensureShader(ctx);

    std::vector<MaskContour> contours;
    collectContours(frame, contours);

    // Mask size matches the output frame size
    rasterizer_->rasterize(contours, cfg_.edgeSoftness,
                           frame.imageWidth, frame.imageHeight);
    lastOutputWidth_  = frame.imageWidth;
    lastOutputHeight_ = frame.imageHeight;
}

void MaskedRecolorEffect::render(const TextureHandle& inputTex,
                                 Framebuffer* outputFbo,
                                 RenderContext* ctx) {
    if (!recolorShader_) return;

    ctx->bindFramebuffer(outputFbo);
    recolorShader_->use();
    recolorShader_->bindTexture("uInputFrame", inputTex, 0);
    if (rasterizer_->mask()) {
        recolorShader_->bindTexture("uMask", *rasterizer_->mask(), 1);
    }
    recolorShader_->setUniform("uTargetColor",
                               cfg_.colorR, cfg_.colorG, cfg_.colorB);
    recolorShader_->setUniform("uOpacity", cfg_.opacity);
    recolorShader_->setUniform("uLuminancePreserve", cfg_.luminancePreserve);

    ctx->drawFullscreenQuad(recolorShader_.get());
    ctx->flush();
}

}  // namespace community_ar
