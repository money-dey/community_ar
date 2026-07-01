// beauty_shader_p6_glow.h
// =============================================================================
// P6 — Glow finishing (warmth, highlight lift, clarity)
//
// Input:  uSource (full-res RGBA8 from P5/P5.5)
//         uSkinMask (full-res R8)
//         uWarmth, uHighlightLift, uClarity
//         uBaselineLuma
// Output: full-res RGBA8 with finishing applied
//
// Three operations, all applied within the skin mask:
//
//   1. Warmth — shifts Oklab a (red-green) and b (yellow-blue) toward
//      warmer hues. Negative warmth shifts toward cool. The shift is
//      proportional to (1 - L)*0.5 + 0.5 so it affects shadows less
//      than midtones (preserves contrast).
//
//   2. Highlight lift — raises L in the midtone-to-highlight range.
//      Operates on a smoothstep window so shadows are untouched and
//      pure highlights don't blow out. Critical for low-light selfies
//      where the face is underexposed.
//
//   3. Clarity — local-contrast enhancement. Sample a small neighborhood
//      average, compute the deviation of the current pixel from that
//      average, scale the deviation by clarity. This is a cheap unsharp-
//      mask. At higher clarity values it can look "crunchy" — the
//      uClarity max of 0.5 in the config prevents over-application.
//
// Tone awareness: highlight lift uses baselineLuma to bias the smoothstep
// window. On dark skin, lift kicks in at lower L values; on light skin,
// only the genuinely-lit areas get lifted.
//
// Performance: ~0.6 ms on Snapdragon 7-class.
// =============================================================================

#pragma once

#include "beauty_shader_common.h"
#include <string>

namespace community_ar {

inline constexpr const char* kP6VS = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)GLSL";

inline constexpr const char* kP6FS_Base = R"GLSL(#version 300 es
precision mediump float;

uniform sampler2D uSource;
uniform sampler2D uSkinMask;
uniform float     uWarmth;          // [-0.2, 0.2]
uniform float     uHighlightLift;   // [0, 0.3]
uniform float     uClarity;         // [0, 0.5]
uniform float     uBaselineLuma;
uniform vec2      uTexelSize;

in  vec2 vUv;
out vec4 fragColor;

// (kBeautyShaderCommon helpers concatenated here at build time)

void main() {
    vec3 srgb = texture(uSource, vUv).rgb;
    float mask = texture(uSkinMask, vUv).r;

    if (mask < 0.005) {
        fragColor = vec4(srgb, 1.0);
        return;
    }

    vec3 lab = srgbToOklab(srgb);

    // ---- Warmth ----
    // Push Oklab a slightly toward red, Oklab b slightly toward yellow.
    // Effect strength tapers with L so shadows are barely touched
    // (they have less chroma headroom and look weird if pushed).
    float warmthScale = mix(0.5, 1.0, lab.x) * mask;
    lab.y += uWarmth * 0.05 * warmthScale;   // a: red-green
    lab.z += uWarmth * 0.08 * warmthScale;   // b: yellow-blue

    // ---- Highlight lift ----
    // Tone-adjusted window. On lighter skin we lift only highlight pixels;
    // on darker skin the lift kicks in lower in the L range.
    float liftWindowLow  = mix(0.35, 0.55, uBaselineLuma);
    float liftWindowHigh = mix(0.75, 0.90, uBaselineLuma);
    float liftMask = smoothstep(liftWindowLow, liftWindowHigh, lab.x);
    lab.x += uHighlightLift * liftMask * mask;

    // ---- Clarity (local-contrast enhancement) ----
    if (uClarity > 0.01) {
        // 4-tap diamond neighborhood average
        vec2 ts = uTexelSize * 2.0;
        float n  = srgbToOklab(texture(uSource, vUv + vec2(0.0, -ts.y)).rgb).x;
        float s  = srgbToOklab(texture(uSource, vUv + vec2(0.0,  ts.y)).rgb).x;
        float e  = srgbToOklab(texture(uSource, vUv + vec2( ts.x, 0.0)).rgb).x;
        float w  = srgbToOklab(texture(uSource, vUv + vec2(-ts.x, 0.0)).rgb).x;
        float avg = (n + s + e + w) * 0.25;
        float deviation = lab.x - avg;
        // Scale clarity application by mask AND by a smoothstep on the
        // deviation magnitude — prevents clarity from amplifying noise.
        float clarityScale = smoothstep(0.005, 0.05, abs(deviation));
        lab.x += deviation * uClarity * clarityScale * mask;
    }

    fragColor = vec4(oklabToSrgb(lab), 1.0);
}
)GLSL";

inline std::string makeP6FragmentShader() {
    std::string fs = kP6FS_Base;
    std::string marker = "// (kBeautyShaderCommon helpers concatenated here at build time)";
    auto pos = fs.find(marker);
    if (pos != std::string::npos) {
        fs.replace(pos, marker.size(), kBeautyShaderCommon);
    }
    return fs;
}

}  // namespace community_ar
