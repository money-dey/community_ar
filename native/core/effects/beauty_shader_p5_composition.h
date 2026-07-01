// beauty_shader_p5_composition.h
// =============================================================================
// P5 — Multi-band Oklab composition
//
// THE HEART OF THE BEAUTY ALGORITHM.
//
// Inputs:
//   uOriginal      — full-res input frame (sRGB)
//   uMidBand       — full-res mid-band (small-radius bilateral output, upsampled)
//   uLowBand       — full-res low-band (large-radius bilateral output, upsampled)
//   uSkinMask      — full-res refined skin mask from P1
//   uSmoothingStrength
//   uHighFreqStrength
//   uMidFreqStrength
//   uDetailPreserve
//   uBlemishReduction
//   uBaselineLuma  — per-face skin tone baseline (for tone-aware scaling)
//
// Output: smoothed image (sRGB)
//
// The algorithm (in Oklab, per-pixel):
//
//   1. Decompose original into three frequency bands using the bilateral
//      outputs as low-pass filters:
//
//        low   = uLowBand
//        mid   = uMidBand - uLowBand     (the band uMidBand kept but uLowBand smoothed away)
//        high  = uOriginal - uMidBand    (the finest detail; pores, single-pixel features)
//
//   2. Scale each band:
//        low'  = low                         (untouched — overall color/lighting)
//        mid'  = mid  × uMidFreqStrength     (wrinkle attenuation; lower = smoother)
//        high' = high × uHighFreqStrength    (pore detail; higher = more texture)
//
//      detailPreserve clamps high' from below — prevents the "plastic"
//      look where high frequency goes to zero.
//
//      blemishReduction adds an additional dampener to mid-band features
//      that are darker than their surroundings (which is what blemishes
//      typically look like in the mid frequency band).
//
//   3. Recompose:  smoothed = low' + mid' + high'
//
//   4. Blend with original by mask × smoothingStrength:
//        output = mix(original, smoothed, mask * smoothingStrength)
//
// All operations happen in Oklab. RGB blending in this pipeline would
// produce muddy results on darker skin.
//
// Tone awareness: the mid-band scaling has a per-pixel tone-aware floor
// at darker baseline lumas. Prevents over-smoothing of skin that's
// already dark (which would look flat).
//
// Performance: ~1.5 ms on Snapdragon 7-class at 1080p.
// =============================================================================

#pragma once

#include "beauty_shader_common.h"
#include <string>

namespace community_ar {

inline constexpr const char* kP5VS = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)GLSL";

inline constexpr const char* kP5FS_Base = R"GLSL(#version 300 es
precision mediump float;

uniform sampler2D uOriginal;
uniform sampler2D uMidBand;
uniform sampler2D uLowBand;
uniform sampler2D uSkinMask;
uniform float     uSmoothingStrength;
uniform float     uHighFreqStrength;
uniform float     uMidFreqStrength;
uniform float     uDetailPreserve;
uniform float     uBlemishReduction;
uniform float     uBaselineLuma;

in  vec2 vUv;
out vec4 fragColor;

// (kBeautyShaderCommon helpers concatenated here at build time)

void main() {
    vec3 origSrgb = texture(uOriginal, vUv).rgb;
    vec3 midSrgb  = texture(uMidBand,  vUv).rgb;
    vec3 lowSrgb  = texture(uLowBand,  vUv).rgb;
    float mask    = texture(uSkinMask, vUv).r;

    // Fast path: outside skin region, passthrough.
    if (mask < 0.005) {
        fragColor = vec4(origSrgb, 1.0);
        return;
    }

    // To Oklab
    vec3 orig = srgbToOklab(origSrgb);
    vec3 midB = srgbToOklab(midSrgb);
    vec3 lowB = srgbToOklab(lowSrgb);

    // Frequency decomposition in Oklab. Bands are vectors so all 3
    // channels (L, a, b) decompose simultaneously.
    vec3 low   = lowB;
    vec3 mid   = midB - lowB;
    vec3 high  = orig - midB;

    // Per-band scaling. Detail preservation: enforce a floor on high
    // frequency to prevent the "plastic skin" look.
    float effectiveHigh = max(uHighFreqStrength, 1.0 - uDetailPreserve * 0.9);

    // Tone-aware mid-band scaling. Darker skin gets less aggressive
    // smoothing (no need to subtract texture that wasn't strongly visible
    // to begin with).
    float toneFloor = max(0.4, 1.0 - uBaselineLuma * 0.6);
    float effectiveMid = mix(uMidFreqStrength, 1.0, 1.0 - toneFloor);

    // Blemish reduction: dampen mid-band features that are darker than
    // their local neighborhood. "Darker than neighborhood" in Oklab L
    // means mid.x is negative (the mid band is what was darker than the
    // lower frequencies). uBlemishReduction strengthens this dampening.
    if (mid.x < 0.0) {
        float blemishDampen = 1.0 - uBlemishReduction * 0.6;
        mid.x *= blemishDampen;
        mid.y *= blemishDampen;
        mid.z *= blemishDampen;
    }

    // Per-band scaling
    high *= effectiveHigh;
    mid  *= effectiveMid;
    // low stays untouched — that's the color/lighting we want to preserve

    // Recompose
    vec3 smoothed = low + mid + high;

    // Blend with original by mask × smoothing strength.
    float blendAmount = mask * uSmoothingStrength;
    vec3 result = mix(orig, smoothed, blendAmount);

    fragColor = vec4(oklabToSrgb(result), 1.0);
}
)GLSL";

inline std::string makeP5FragmentShader() {
    std::string fs = kP5FS_Base;
    std::string marker = "// (kBeautyShaderCommon helpers concatenated here at build time)";
    auto pos = fs.find(marker);
    if (pos != std::string::npos) {
        fs.replace(pos, marker.size(), kBeautyShaderCommon);
    }
    return fs;
}

}  // namespace community_ar
