// skin_tone.h
// =============================================================================
// Community AR — Skin tone estimator
//
// Produces a `SkinToneEstimate` from a camera frame + face landmarks.
//
// Approach:
//   1. Once per ~5 frames (skin tone doesn't change frame-to-frame), trigger
//      a GPU compute pass that samples N points in the skin region.
//   2. The compute pass writes RGB triples to a small SSBO (or 1xN texture).
//   3. CPU reads back the SSBO (~1KB), trims outliers, computes mean.
//   4. The estimate is exposed to effect shaders as a uniform.
//
// Sampling is stratified across the skin region (cheeks + forehead) using
// landmark-derived sample points — no neural network needed for sampling.
// Outlier rejection (specular hotspots, shadows) is done via trimmed mean.
//
// IMPORTANT: GPU readback to CPU is the slowest operation on mobile graphics.
// We mitigate by:
//   - Reading only a tiny buffer (~256 samples × 4 bytes = 1KB)
//   - Doing it asynchronously every Nth frame, not every frame
//   - Using PBO/MTLBuffer with persistent mapping so the read doesn't stall
// =============================================================================

#pragma once

#include "perception_frame.h"

namespace community_ar {

class RenderContext;
class TextureHandle;

class SkinToneEstimator {
public:
    explicit SkinToneEstimator(RenderContext* ctx);
    ~SkinToneEstimator();

    // Run sampling on the current camera frame. The result is asynchronous —
    // it may not be available until 1-2 frames later (PBO/buffer readback).
    // Call this every N frames; the estimator handles staleness internally.
    void requestUpdate(const TextureHandle& cameraTex,
                       const FaceLandmarks& landmarks,
                       int frameId);

    // Returns the most recent estimate. Initially invalid until the first
    // readback completes.
    SkinToneEstimate getCurrent() const;

    // Force-reset (e.g. when the user switches camera)
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace community_ar
