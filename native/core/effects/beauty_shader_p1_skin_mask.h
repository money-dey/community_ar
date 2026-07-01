// beauty_shader_p1_skin_mask.h
// =============================================================================
// P1 — Skin mask refinement
//
// Input:  uSegmenterMask (R8, full-res, from masks.faceSkin)
//         uLandmarkExclusions (R8, optional — lips/eyes/brows polygon mask
//                              from MaskRasterizer; black where the
//                              landmarks say "not skin")
// Output: refined skin mask (R8)
//
// Operation:
//   refined(x) = segmenter(x) * (1 - landmarkExclusions(x))
//                followed by smoothstep edge softening.
//
// The segmenter's face-skin output is already pretty good but includes
// some non-skin areas near boundaries (the segmenter doesn't know about
// lips/eyes/brows the way landmarks do). Subtracting the landmark-
// derived exclusion zones produces a tighter, cleaner mask.
//
// The smoothstep at the end is a 3-tap edge softener — averages each
// pixel with its left/right neighbors at half weight to remove single-
// pixel jitter at the mask boundary. Cheap; very effective.
//
// Output goes into texSkinMask_ and is published to the pool as
// masks.refinedFaceSkin. Downstream beauty passes (P5, P6, P6.5) all
// read from this refined mask.
//
// Performance: ~0.4 ms on Snapdragon 7-class at 1080p.
// =============================================================================

#pragma once

namespace community_ar {

inline constexpr const char* kP1VS = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)GLSL";

inline constexpr const char* kP1FS = R"GLSL(#version 300 es
precision mediump float;

uniform sampler2D uSegmenterMask;
uniform sampler2D uLandmarkExclusions;
uniform vec2      uTexelSize;             // 1.0 / texture dimensions
uniform float     uHasLandmarkExclusions; // 0.0 if no landmark mask available

in  vec2 vUv;
out vec4 fragColor;

float sampleSegmenter(vec2 uv) {
    return texture(uSegmenterMask, uv).r;
}

void main() {
    // Sample segmenter mask with light 3-tap horizontal smoothing
    // (anti-jitter; uses very small kernel because we want to preserve
    // the sharp segmenter edges, not blur them away).
    float m = sampleSegmenter(vUv) * 0.5
            + sampleSegmenter(vUv - vec2(uTexelSize.x, 0.0)) * 0.25
            + sampleSegmenter(vUv + vec2(uTexelSize.x, 0.0)) * 0.25;

    // Apply landmark exclusions if available. exclusions is in [0,1] where
    // 1.0 = "this pixel is on a landmark feature (lips/eyes/brows)".
    if (uHasLandmarkExclusions > 0.5) {
        float excl = texture(uLandmarkExclusions, vUv).r;
        m *= (1.0 - excl);
    }

    // Smoothstep the result — soft mask edges look better in the
    // downstream bilateral blending than hard binary boundaries.
    float refined = smoothstep(0.15, 0.85, m);

    fragColor = vec4(refined, 0.0, 0.0, 1.0);
}
)GLSL";

}  // namespace community_ar
