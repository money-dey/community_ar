// hair_segmenter_backend.cpp
// =============================================================================
// HairSegmenterBackend implementation.
//
// Delegates to the Phase 1 HairSegmenter class, wraps its output in the
// SegmentationChannels structure required by the new SegmenterBackend
// interface, and tracks inference time for diagnostics.
// =============================================================================

#include "hair_segmenter_backend.h"
#include "perception_models.h"  // for HairSegmenter (Phase 1)
#include "../ml/neural_backend.h"
#include <chrono>

namespace community_ar {

HairSegmenterBackend::HairSegmenterBackend(NeuralBackend* neuralBackend,
                                           const std::string& modelPath) {
    impl_ = std::make_unique<HairSegmenter>(neuralBackend, modelPath);
}

HairSegmenterBackend::~HairSegmenterBackend() = default;

SegmentationChannels HairSegmenterBackend::run(const TextureHandle& cameraTex,
                                               RenderContext* ctx) {
    auto t0 = std::chrono::steady_clock::now();

    // Phase 1's HairSegmenter::run() returns a single mask texture.
    // We adapt it into the multi-channel structure.
    auto hairMask = impl_->run(cameraTex, ctx);

    auto t1 = std::chrono::steady_clock::now();
    lastInferenceMs_ = std::chrono::duration<float, std::milli>(t1 - t0).count();

    SegmentationChannels channels;
    channels.backendName = "HairSegmenter";
    channels.fresh = (hairMask != nullptr);

    // We received a shared_ptr<TextureHandle>-equivalent from the Phase 1
    // implementation. Pass through.
    channels.hair = hairMask;
    // All other channels stay nullptr — that's the contract for this backend.

    return channels;
}

}  // namespace community_ar
