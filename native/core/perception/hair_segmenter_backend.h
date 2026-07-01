// hair_segmenter_backend.h
// =============================================================================
// HairSegmenterBackend
//
// Implements SegmenterBackend on top of the Phase 1 hair_segmenter.tflite
// model. Produces ONLY the hair channel; all others are nullptr.
//
// This is the fallback backend. It exists to keep the system working if:
//   - The multiclass model's verification gate (M7 in car-phase-3-dag.md) fails
//   - A user explicitly opts out of the larger model (binary size concerns)
//   - We're testing the architecture without the new model present
//
// Implementation notes:
//   - The underlying inference code is the same as Phase 1's
//     `HairSegmenter`. This class is a thin adapter, not a rewrite.
//   - We don't drop the Phase 1 code when adopting multiclass; it stays
//     as the documented fallback path.
// =============================================================================

#pragma once

#include "segmenter_backend.h"
#include <chrono>

namespace community_ar {

class NeuralBackend;
class HairSegmenter;  // Phase 1 implementation, exists already

class HairSegmenterBackend : public SegmenterBackend {
public:
    // The neuralBackend (TFLite/Core ML) and modelPath are configured at
    // session level. modelPath must point to the hair_segmenter.tflite
    // file fetched by tools/fetch_models.sh.
    HairSegmenterBackend(NeuralBackend* neuralBackend,
                         const std::string& modelPath);
    ~HairSegmenterBackend() override;

    std::string name() const override { return "HairSegmenter"; }
    float lastInferenceMs() const override { return lastInferenceMs_; }

    ChannelsProduced channelsProduced() const override {
        ChannelsProduced cp;
        cp.hair = true;
        return cp;
    }

    SegmentationChannels run(const TextureHandle& cameraTex,
                             RenderContext* ctx) override;

private:
    std::unique_ptr<HairSegmenter> impl_;
    float lastInferenceMs_ = 0.0f;
};

}  // namespace community_ar
