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
#include "../render/render_context.h"  // for TextureHandle width()/height()
#include <chrono>

namespace community_ar {

HairSegmenterBackend::HairSegmenterBackend(NeuralBackend* neuralBackend,
                                           const std::string& modelPath)
    : neuralBackend_(neuralBackend), modelPath_(modelPath) {}

HairSegmenterBackend::~HairSegmenterBackend() = default;

SegmentationChannels HairSegmenterBackend::run(const TextureHandle& cameraTex,
                                               RenderContext* ctx) {
    // HairSegmenter (Phase 1) needs a RenderContext, only available here.
    // Construct and initialize it on the first run().
    if (!impl_) {
        impl_ = std::make_unique<HairSegmenter>(neuralBackend_, ctx);
        impl_->initialize();
    }

    auto t0 = std::chrono::steady_clock::now();

    // Phase 1's HairSegmenter::run() writes a borrowed mask texture pointer
    // (valid until its next run()) and returns success.
    const TextureHandle* hairMask = nullptr;
    bool ok = impl_->run(cameraTex, cameraTex.width(), cameraTex.height(),
                         &hairMask);

    auto t1 = std::chrono::steady_clock::now();
    lastInferenceMs_ = std::chrono::duration<float, std::milli>(t1 - t0).count();

    SegmentationChannels channels;
    channels.backendName = "HairSegmenter";
    channels.fresh = ok && (hairMask != nullptr);

    // The mask is owned by HairSegmenter (borrowed handle). Wrap it in a
    // non-owning shared_ptr (no-op deleter) so it satisfies the
    // SegmentationChannels contract without transferring ownership — matching
    // the documented "valid until the next run()" lifetime.
    if (channels.fresh) {
        channels.hair = std::shared_ptr<TextureHandle>(
            const_cast<TextureHandle*>(hairMask), [](TextureHandle*) {});
    }
    // All other channels stay nullptr — that's the contract for this backend.

    return channels;
}

}  // namespace community_ar
