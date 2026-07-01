// beauty_shader_p3_bilateral.h
// =============================================================================
// P3 — Bilateral filter (separable, parameterized)
//
// Input:  uSource (half-res RGBA8)
// Output: half-res RGBA8 (smoothed along configured axis)
//
// Operation:
//   Separable bilateral filter, run 4 times per frame (P3a-d):
//     - P3a: horizontal, small radius
//     - P3b: vertical, small radius
//     - P3c: horizontal, large radius
//     - P3d: vertical, large radius
//
//   "Small radius" produces the mid-band: blurs sub-pixel skin texture
//   without affecting wrinkles or larger features.
//   "Large radius" produces the low-band: blurs everything mid-scale,
//   leaving only overall lighting and color.
//
//   The classic bilateral formula:
//     weight(neighbor) = spatial(distance) × range(color_diff)
//   The range term is what makes bilateral edge-preserving — strongly
//   different colors don't contribute to each other's blur.
//
// Edge weight is computed in Oklab L (luminance), not RGB. This is
// critical for working across skin tones: an L-based edge threshold
// behaves the same on dark and light skin, whereas an RGB-based one
// would be more sensitive on light skin (small color differences look
// large) and less on dark (small color differences are washed out).
//
// Tone-awareness: the edge sensitivity threshold scales by baselineLuma
// so darker skin gets proportionally lower thresholds (preserving
// equivalent "amount of texture removal" across skin tones).
//
// Performance: ~1.2 ms per pass on Snapdragon 7-class at half-res
// (~5 ms total for 4 passes).
// =============================================================================

#pragma once

#include "beauty_shader_common.h"
#include <string>

namespace community_ar {

inline constexpr const char* kP3VS = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)GLSL";

inline constexpr const char* kP3FS_Base = R"GLSL(#version 300 es
precision mediump float;

uniform sampler2D uSource;
uniform vec2      uTexelSize;     // 1.0 / source dimensions
uniform vec2      uAxis;          // (1,0) horizontal pass, (0,1) vertical
uniform float     uRadius;        // 1.0 = "small", 3.0 = "large"
uniform float     uEdgeSensitivity; // user's bilateralEdgeSensitivity * tone scale
uniform float     uBaselineLuma;  // for tone-aware threshold adjustment

in  vec2 vUv;
out vec4 fragColor;

// (kBeautyShaderCommon helpers concatenated here at build time)

void main() {
    vec3 center = texture(uSource, vUv).rgb;
    float centerL = srgbToOklab(center).x;

    // Tone-adjusted edge sensitivity. Darker skin → lower threshold.
    float threshold = max(0.02, uEdgeSensitivity * max(0.25, uBaselineLuma));
    float rangeScale = 1.0 / (threshold * threshold);

    // 7-tap kernel along uAxis with Gaussian spatial weights and
    // range weights computed in Oklab L space.
    vec3 sum = center;
    float weightSum = 1.0;

    // Tap offsets: 1, 2, 3 texels times radius
    for (float i = 1.0; i <= 3.0; i += 1.0) {
        vec2 off = uAxis * uTexelSize * uRadius * i;

        vec3 plus  = texture(uSource, vUv + off).rgb;
        vec3 minus = texture(uSource, vUv - off).rgb;

        // Spatial weights — Gaussian falloff
        float spatial = exp(-(i * i) * 0.5);

        // Range weights — Oklab L difference
        float dPlus  = srgbToOklab(plus).x - centerL;
        float dMinus = srgbToOklab(minus).x - centerL;
        float wPlus  = spatial * exp(-(dPlus  * dPlus)  * rangeScale);
        float wMinus = spatial * exp(-(dMinus * dMinus) * rangeScale);

        sum += plus  * wPlus;
        sum += minus * wMinus;
        weightSum += wPlus + wMinus;
    }

    sum /= weightSum;
    fragColor = vec4(sum, 1.0);
}
)GLSL";

// Compose the full fragment shader by inserting beauty_shader_common helpers
// after the precision declaration. The Marker line is what we split on.
inline std::string makeP3FragmentShader() {
    std::string fs = kP3FS_Base;
    // Insert kBeautyShaderCommon right before the "void main()" declaration
    std::string marker = "// (kBeautyShaderCommon helpers concatenated here at build time)";
    auto pos = fs.find(marker);
    if (pos != std::string::npos) {
        fs.replace(pos, marker.size(), kBeautyShaderCommon);
    }
    return fs;
}

}  // namespace community_ar
