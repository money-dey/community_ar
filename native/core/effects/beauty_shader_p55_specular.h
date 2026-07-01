// beauty_shader_p55_specular.h
// =============================================================================
// P5.5 — Specular control (matte ↔ glow)
//
// Input:  uSource (full-res RGBA8 from P5 output)
//         uSkinMask (full-res R8)
//         uSpecularControl   — [-1, 1], eased per-frame
//         uBaselineLuma      — per-face baseline
// Output: full-res RGBA8 with adjusted specular character
//
// Operation:
//   Specular pixels are detected as those with high Oklab L AND low
//   chroma (highlight reflections look near-white). The amount of
//   specular adjustment to apply depends on uSpecularControl:
//
//     uSpecularControl < 0  (matte):
//       Reduce L on specular pixels. Suppresses highlights, makes skin
//       look less shiny. Strongest at -1.0.
//
//     uSpecularControl > 0  (glow):
//       Boost L on specular pixels AND add a small soft glow. Makes
//       skin look more radiant. Strongest at +1.0.
//
//     uSpecularControl ≈ 0:
//       Passthrough.
//
//   The detection threshold scales by baselineLuma — what counts as "a
//   highlight" depends on how bright the underlying skin is. On dark
//   skin, smaller L values qualify as specular; on light skin, only
//   the brightest pixels do.
//
// Where the easing happens:
//   uSpecularControl arrives pre-eased from the C++ side (see
//   SkinSmoothEffect::easeSpecular). This shader treats it as a static
//   value for the frame.
//
// Performance: ~0.4 ms on Snapdragon 7-class.
// Skipped entirely on Low quality tier.
// =============================================================================

#pragma once

#include "beauty_shader_common.h"
#include <string>

namespace community_ar {

inline constexpr const char* kP55VS = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)GLSL";

inline constexpr const char* kP55FS_Base = R"GLSL(#version 300 es
precision mediump float;

uniform sampler2D uSource;
uniform sampler2D uSkinMask;
uniform float     uSpecularControl;  // [-1, 1], eased
uniform float     uBaselineLuma;
uniform vec2      uTexelSize;

in  vec2 vUv;
out vec4 fragColor;

// (kBeautyShaderCommon helpers concatenated here at build time)

void main() {
    vec3 srgb = texture(uSource, vUv).rgb;
    float mask = texture(uSkinMask, vUv).r;

    // Outside skin or near-zero specular control: passthrough
    if (mask < 0.005 || abs(uSpecularControl) < 0.01) {
        fragColor = vec4(srgb, 1.0);
        return;
    }

    vec3 lab = srgbToOklab(srgb);

    // Tone-aware specular threshold: scaled by baselineLuma.
    // Bright lighting on dark skin produces specular highlights that
    // are still relatively low in absolute L, but high relative to the
    // surrounding skin. A baseline-scaled threshold catches both.
    float specularThreshold = mix(0.55, 0.85, uBaselineLuma);

    // Chroma magnitude (Oklab a,b distance from origin)
    float chroma = length(lab.yz);

    // Pixel "specular-ness": high L, low chroma
    //   specularness = smoothstep(thresh, thresh+0.15, L) * (1 - chroma * 3)
    float lScore = smoothstep(specularThreshold,
                              specularThreshold + 0.15, lab.x);
    float chromaPenalty = clamp(1.0 - chroma * 3.0, 0.0, 1.0);
    float specularness = lScore * chromaPenalty * mask;

    if (uSpecularControl < 0.0) {
        // Matte: pull L down on specular pixels
        float matteFactor = -uSpecularControl;          // [0, 1]
        float lReduction  = specularness * matteFactor * 0.18;
        lab.x -= lReduction;
    } else {
        // Glow: push L up, add soft glow contribution from neighborhood
        float glowFactor = uSpecularControl;            // [0, 1]

        // Self-boost
        lab.x += specularness * glowFactor * 0.15;

        // Soft local glow: sample a few neighbors, find their max
        // specularness, scale by their L. This produces the "bloom"
        // effect on the skin around bright spots.
        vec3 glowSrgb = vec3(0.0);
        float glowWeight = 0.0;
        for (float dy = -1.0; dy <= 1.0; dy += 1.0) {
            for (float dx = -1.0; dx <= 1.0; dx += 1.0) {
                if (dx == 0.0 && dy == 0.0) continue;
                vec2 nUv = vUv + vec2(dx, dy) * uTexelSize * 3.0;
                vec3 nSrgb = texture(uSource, nUv).rgb;
                vec3 nLab = srgbToOklab(nSrgb);
                float nL = smoothstep(specularThreshold,
                                       specularThreshold + 0.15, nLab.x);
                glowSrgb += nSrgb * nL;
                glowWeight += nL;
            }
        }
        if (glowWeight > 0.01) {
            glowSrgb /= glowWeight;
            vec3 glowAdd = (glowSrgb - srgb) * glowFactor * 0.08;
            // Convert back from sRGB-space glow add to a small L bump
            lab.x += dot(glowAdd, vec3(0.3, 0.6, 0.1)) * mask;
        }
    }

    fragColor = vec4(oklabToSrgb(lab), 1.0);
}
)GLSL";

inline std::string makeP55FragmentShader() {
    std::string fs = kP55FS_Base;
    std::string marker = "// (kBeautyShaderCommon helpers concatenated here at build time)";
    auto pos = fs.find(marker);
    if (pos != std::string::npos) {
        fs.replace(pos, marker.size(), kBeautyShaderCommon);
    }
    return fs;
}

}  // namespace community_ar
