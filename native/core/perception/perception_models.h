// perception_models.h
// =============================================================================
// Community AR — Segmenter model wrappers
//
// Hair Segmenter and Selfie Segmenter. Iris detection moved to its own file
// (iris_landmarker.{h,cpp}) — see that file for the per-track, two-eye
// implementation. The segmenters here remain whole-image mask producers.
// =============================================================================

#pragma once

#include "perception_frame.h"
#include "../ml/neural_backend.h"
#include <memory>

namespace community_ar {

class TextureHandle;
class RenderContext;

// -----------------------------------------------------------------------------
// HairSegmenter — MediaPipe Hair Segmenter
//
// Input: full camera frame (typically downsampled to 256x256 by the model).
// Output: a single-channel mask texture (1 = hair, 0 = not).
// We render this output into a TextureHandle for downstream effect use.
// -----------------------------------------------------------------------------
class HairSegmenter {
public:
    HairSegmenter(NeuralBackend* backend, RenderContext* ctx);
    ~HairSegmenter();

    bool initialize();

    // Returns a borrowed handle to the hair mask. Valid until the next run().
    bool run(const TextureHandle& cameraTex,
             int imageWidth, int imageHeight,
             const TextureHandle** outMask);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// -----------------------------------------------------------------------------
// SelfieSegmenter — MediaPipe Selfie Segmenter
//
// Same shape as HairSegmenter but for body/person segmentation, used by
// BackgroundEffect.
// -----------------------------------------------------------------------------
class SelfieSegmenter {
public:
    SelfieSegmenter(NeuralBackend* backend, RenderContext* ctx);
    ~SelfieSegmenter();

    bool initialize();

    bool run(const TextureHandle& cameraTex,
             int imageWidth, int imageHeight,
             const TextureHandle** outMask);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace community_ar
