// beauty_shader_p65_temporal.h
// =============================================================================
// P6.5 — Temporal stabilization (motion-gated history blend)
//
// Input:  uSource (full-res RGBA8 from P6 output, this frame's beauty result)
//         uPrevious (full-res RGBA8 from last frame's final output)
//         uTemporalSmoothing  — [0, 1], user-configured blend strength
//         uMotionMagnitude    — [0, 1], coarse face-motion estimate
// Output: full-res RGBA8 (blended), AND a copy to history buffer for next frame
//
// Operation:
//   When the subject is stationary, frame-to-frame variation in the
//   bilateral and mask boundaries produces visible jitter ("crawling
//   skin"). Blending with the previous frame's result suppresses this
//   jitter without affecting actual moving content.
//
//   When the subject moves, the previous frame's content is in a
//   different position — blending creates "ghosting" or trails.
//   Motion-gated blend: high motion → use mostly current frame.
//
//   Disocclusion guard: motion > 0.05 (face-size normalized) → bypass
//   blend entirely. Prevents the most-visible ghosting cases (head
//   turns, rapid speech).
//
// Motion estimation:
//   uMotionMagnitude comes from PerceptionFrame.faces[i].motion (Phase 1
//   produces this; it's the per-face translation magnitude in normalized
//   image coordinates). The whole-frame motion is the max across visible
//   faces — if any face is moving, we should de-bias the temporal blend
//   for the whole frame.
//
//   This is "coarse" motion estimation: it doesn't actually know
//   whether the pixel at this location moved. A future improvement is
//   per-pixel optical flow (Phase 5+ task; for Phase 3, coarse is good
//   enough because most of the time the subject is either stationary
//   or moving as a whole).
//
// First-frame behavior:
//   On the very first frame after activation, no history exists. The
//   C++ side handles this by initializing the history buffer with the
//   current beauty output before this shader runs (so first frame's
//   temporal blend is effectively identity — see "first-frame temporal"
//   open question Q3 resolution in car-phase-3-requirements.md).
//
// Performance: ~0.4 ms on Snapdragon 7-class.
// =============================================================================

#pragma once

namespace community_ar {

inline constexpr const char* kP65VS = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)GLSL";

inline constexpr const char* kP65FS = R"GLSL(#version 300 es
precision mediump float;

uniform sampler2D uSource;
uniform sampler2D uPrevious;
uniform float     uTemporalSmoothing;  // [0, 1]
uniform float     uMotionMagnitude;    // [0, 1]

in  vec2 vUv;
out vec4 fragColor;

// Disocclusion threshold — above this motion level, skip temporal blend
// entirely. Face-size-normalized; 0.05 means "face moved more than 5%
// of its own size in one frame."
const float kDisocclusionThreshold = 0.05;

void main() {
    vec3 cur = texture(uSource, vUv).rgb;

    // Disocclusion guard: high motion → just output current frame
    if (uMotionMagnitude > kDisocclusionThreshold) {
        fragColor = vec4(cur, 1.0);
        return;
    }

    vec3 prev = texture(uPrevious, vUv).rgb;

    // Compute effective blend weight: user-configured smoothing,
    // reduced by motion. At zero motion: full uTemporalSmoothing.
    // At threshold motion: zero (smooth transition to current-only).
    float motionScale = 1.0 - smoothstep(0.0, kDisocclusionThreshold,
                                          uMotionMagnitude);
    float blendWeight = uTemporalSmoothing * motionScale;

    vec3 result = mix(cur, prev, blendWeight);
    fragColor = vec4(result, 1.0);
}
)GLSL";

}  // namespace community_ar
