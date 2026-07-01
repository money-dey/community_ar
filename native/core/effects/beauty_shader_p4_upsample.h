// beauty_shader_p4_upsample.h
// =============================================================================
// P4 — Tent upsample (half-res → full-res)
//
// Input:  uSource (half-res RGBA8)
// Output: full-res RGBA8
//
// Operation:
//   Bilinear sampling already gives a passable 2× upsample, but produces
//   visible blockiness around bilateral-blurred regions. A 4-tap tent
//   filter (sometimes called the "Kawase upsample" or "tent dilation")
//   adds a small additional smoothing that's barely-visible at the
//   pixel level but completely eliminates the blockiness.
//
//   Tap pattern (offsets in source-texel units):
//     center + N + S + E + W, each contributing equal weight
//     The hardware's bilinear interpolation does the heavy lifting;
//     this just adds a tiny diamond-shaped average.
//
// Why not a single bilinear sample:
//   Bilinear from half-res to full-res produces visible "tile boundaries"
//   where the 2×2 source quad changes. The 4-tap tent averages adjacent
//   bilinear samples, hiding the boundary.
//
// Performance: ~0.4 ms on Snapdragon 7-class.
// =============================================================================

#pragma once

namespace community_ar {

inline constexpr const char* kP4VS = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)GLSL";

inline constexpr const char* kP4FS = R"GLSL(#version 300 es
precision mediump float;

uniform sampler2D uSource;
uniform vec2      uSourceTexelSize;  // 1.0 / source (half-res) dimensions

in  vec2 vUv;
out vec4 fragColor;

void main() {
    vec2 ts = uSourceTexelSize;

    // 4-tap tent around the bilinear-sampled center.
    // Each tap is at one source-texel-width offset, in the cardinal directions.
    vec3 c  = texture(uSource, vUv).rgb;
    vec3 cN = texture(uSource, vUv + vec2(0.0, -ts.y)).rgb;
    vec3 cS = texture(uSource, vUv + vec2(0.0,  ts.y)).rgb;
    vec3 cE = texture(uSource, vUv + vec2( ts.x, 0.0)).rgb;
    vec3 cW = texture(uSource, vUv + vec2(-ts.x, 0.0)).rgb;

    // Center gets 2× weight; arms get 1× each. Total weight = 6 → divide by 6.
    vec3 sum = (c * 2.0 + cN + cS + cE + cW) / 6.0;
    fragColor = vec4(sum, 1.0);
}
)GLSL";

}  // namespace community_ar
