// multiclass_segmenter_backend.h
// =============================================================================
// MulticlassSegmenterBackend
//
// Implements SegmenterBackend on top of selfie_multiclass_256x256.tflite.
// One inference pass produces 6 channels: background, hair, body-skin,
// face-skin, clothes, others.
//
// Why this exists (Phase 3 architectural decision D1):
//   - Beauty v2 needs a tight face-skin mask. Landmark-derived skin masks
//     have hard polygon edges; per-pixel neural segmentation has soft
//     edges that beauty v2's bilateral passes can blend cleanly into.
//   - The body-skin channel lets us beautify *just* the face without
//     affecting visible neck/shoulders/arms.
//   - Replaces the dedicated hair_segmenter for Phase 6+ hair effects
//     (one model, three jobs).
//
// Trade-off honestly accounted:
//   - +10MB binary (15.6MB vs 5.0MB)
//   - Same inference cost (wider output, not deeper)
//   - Quality on diverse hair textures and skin tones is the open verification
//     question (car-phase-3-requirements.md, verification checklist)
//
// Verification status (May 2026):
//   - Model file: download URL committed (tools/fetch_models.sh)
//   - Inference path: depends on Phase 1's NeuralBackend abstraction, which
//     is structurally complete but not yet exercised on a real device
//   - Output format: 6-channel HxWx6 float tensor; we split into 6 R8
//     single-channel textures via a small compositing shader (see below)
// =============================================================================

#pragma once

#include "segmenter_backend.h"

namespace community_ar {

class NeuralBackend;
class ShaderProgram;

class MulticlassSegmenterBackend : public SegmenterBackend {
public:
    MulticlassSegmenterBackend(NeuralBackend* neuralBackend,
                               const std::string& modelPath);
    ~MulticlassSegmenterBackend() override;

    std::string name() const override { return "MulticlassSegmenter256"; }
    float lastInferenceMs() const override { return lastInferenceMs_; }

    ChannelsProduced channelsProduced() const override {
        ChannelsProduced cp;
        cp.background = true;
        cp.hair       = true;
        cp.bodySkin   = true;
        cp.faceSkin   = true;
        cp.clothes    = true;
        cp.others     = true;
        return cp;
    }

    SegmentationChannels run(const TextureHandle& cameraTex,
                             RenderContext* ctx) override;

private:
    void ensureChannelTextures(RenderContext* ctx);
    void splitChannels(const TextureHandle& tensorTex,
                       RenderContext* ctx);

    NeuralBackend* neuralBackend_;
    std::string    modelPath_;
    int            modelId_ = -1;       // assigned at first run()
    bool           loaded_ = false;

    float          lastInferenceMs_ = 0.0f;

    // The 6 output channels, allocated once and reused across frames.
    // Each is a 256x256 R8 texture.
    std::shared_ptr<TextureHandle> channelBackground_;
    std::shared_ptr<TextureHandle> channelHair_;
    std::shared_ptr<TextureHandle> channelBodySkin_;
    std::shared_ptr<TextureHandle> channelFaceSkin_;
    std::shared_ptr<TextureHandle> channelClothes_;
    std::shared_ptr<TextureHandle> channelOthers_;

    // Shader that splits the 6-channel model output into 6 single-channel
    // textures. One pass writes all 6 (uses MRT — multiple render targets).
    std::unique_ptr<ShaderProgram> splitShader_;
    std::unique_ptr<Framebuffer>   splitFbo_;  // bound with 6 attachments
};

}  // namespace community_ar
