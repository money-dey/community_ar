// beauty_shader_common.h
// =============================================================================
// Community AR — Shared GLSL helpers for beauty v2 shaders
//
// Every Phase 3 beauty shader that touches color works in Oklab space.
// Repeating the ~40 lines of color-space conversion in each shader file
// would be tedious and error-prone, so we centralize the helpers here as
// a constexpr string that shaders concatenate after their #version line.
//
// Convention: include this header and concatenate kBeautyShaderCommon
// after the GLSL `#version` and `precision` declarations.
//
// All helpers follow Björn Ottosson's Oklab formulation:
//   https://bottosson.github.io/posts/oklab/
// =============================================================================

#pragma once

namespace community_ar {

// -----------------------------------------------------------------------------
// kBeautyShaderCommon
//
// Shared color-space helpers. Inserted after `#version 300 es` and any
// precision declarations. Each helper is a small, pure function.
// -----------------------------------------------------------------------------
inline constexpr const char* kBeautyShaderCommon = R"GLSL(

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

// -- linear RGB <-> Oklab (Björn Ottosson formulation) --
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

// -- Direct sRGB <-> Oklab pairs (convenience) --
vec3 srgbToOklab(vec3 c) { return linearToOklab(srgbToLinear(c)); }
vec3 oklabToSrgb(vec3 c) { return linearToSrgb(clamp(oklabToLinear(c), 0.0, 1.0)); }

// -- Tone-aware threshold scaling --
// The beauty pipeline must work consistently across light and dark skin.
// All threshold parameters that involve luminance (edge detection,
// highlight detection, etc.) scale by max(0.25, baselineLuma) so that
// "what counts as bright" depends on the subject's skin tone.
//
// baselineLuma comes from PerceptionFrame.faces[i].skinTone.baselineLuma
// (range [0, 1], typical values 0.2 for very dark to 0.9 for very light).
float toneAdjust(float threshold, float baselineLuma) {
    return threshold * max(0.25, baselineLuma);
}
)GLSL";

}  // namespace community_ar
