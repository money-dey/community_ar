// perception_models.cpp
// =============================================================================
// Implementations for Hair Segmenter and Selfie Segmenter.
//
// (IrisLandmarker has moved to iris_landmarker.cpp — see that file for the
// production version with both eyes and per-track filtering.)
//
// The remaining segmenters follow this pattern:
//   1. Load the appropriate MediaPipe model on init
//   2. On run(): feed the camera frame to the model
//   3. Materialize the output mask into a GPU texture for downstream sampling
// =============================================================================

#include "perception_models.h"
#include "one_euro_filter.h"
#include "../render/render_context.h"
#include <cmath>
#include <algorithm>

namespace community_ar {

// =============================================================================
// HairSegmenter / SelfieSegmenter
// =============================================================================
// Both segmenters output a low-resolution mask (typically 256x256 single-
// channel float). We upload the result to a GPU texture so effects can sample
// it via standard texture lookups. Upload is cheap (~64KB) and happens once
// per frame.
//
// We keep two separate classes for clarity; they share 90% of the code,
// differing only in the model filename.

namespace {

class SegmenterImpl {
public:
    SegmenterImpl(NeuralBackend* backend, RenderContext* ctx,
                  const char* modelName, int maskW, int maskH)
        : backend_(backend), ctx_(ctx), modelName_(modelName),
          maskW_(maskW), maskH_(maskH) {}

    bool initialize() {
        model_ = backend_->loadModel(modelName_);
        if (!model_) return false;
        maskTex_ = ctx_->createTexture(maskW_, maskH_, TextureHandle::Format::R8);
        return true;
    }

    bool run(const TextureHandle& cameraTex,
             int imageWidth, int imageHeight,
             const TextureHandle** outMask) {
        if (!model_) return false;
        CameraInputRect rect;
        rect.x = 0;
        rect.y = 0;
        rect.width = imageWidth;
        rect.height = imageHeight;
        model_->setInputTexture(0, cameraTex, rect);
        if (!model_->run()) return false;
        // Bind the output mask tensor directly to our GPU texture (zero-copy),
        // keeping the mask GPU-resident. The Phase 1 scaffold instead read the
        // output back to a CPU buffer and left the re-upload commented out, so
        // maskTex_ was never actually populated. Binding matches the pattern
        // the multiclass segmenter uses and the "pixels never leave the GPU"
        // invariant.
        if (!model_->bindOutputTexture(0, maskTex_.get())) return false;
        *outMask = maskTex_.get();
        return true;
    }

private:
    NeuralBackend* backend_;
    RenderContext* ctx_;
    const char* modelName_;
    int maskW_, maskH_;
    std::unique_ptr<NeuralModel> model_;
    std::unique_ptr<TextureHandle> maskTex_;
};

}  // anonymous namespace

struct HairSegmenter::Impl { std::unique_ptr<SegmenterImpl> seg; };
HairSegmenter::HairSegmenter(NeuralBackend* b, RenderContext* c)
    : impl_(std::make_unique<Impl>()) {
    impl_->seg = std::make_unique<SegmenterImpl>(b, c, "hair_segmenter", 256, 256);
}
HairSegmenter::~HairSegmenter() = default;
bool HairSegmenter::initialize() { return impl_->seg->initialize(); }
bool HairSegmenter::run(const TextureHandle& tex, int w, int h,
                        const TextureHandle** out) {
    return impl_->seg->run(tex, w, h, out);
}

struct SelfieSegmenter::Impl { std::unique_ptr<SegmenterImpl> seg; };
SelfieSegmenter::SelfieSegmenter(NeuralBackend* b, RenderContext* c)
    : impl_(std::make_unique<Impl>()) {
    impl_->seg = std::make_unique<SegmenterImpl>(b, c, "selfie_segmenter", 256, 256);
}
SelfieSegmenter::~SelfieSegmenter() = default;
bool SelfieSegmenter::initialize() { return impl_->seg->initialize(); }
bool SelfieSegmenter::run(const TextureHandle& tex, int w, int h,
                          const TextureHandle** out) {
    return impl_->seg->run(tex, w, h, out);
}

}  // namespace community_ar
