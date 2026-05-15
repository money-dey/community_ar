// effect_graph.cpp
// =============================================================================
// EffectGraph implementation.
//
// Routing per frame:
//
//   N effects:                       Reads from        Writes to
//   ────────────────────────────────────────────────────────────────────
//   N = 0  (empty graph)             inputCamera       outputFbo  (blit only)
//   N = 1                            inputCamera       outputFbo
//   N = 2                            inputCamera       chain[0]
//                                    chain[0]          outputFbo
//   N = 3                            inputCamera       chain[0]
//                                    chain[0]          chain[1]
//                                    chain[1]          outputFbo
//   N = K                            ping-pong using chain[0] and chain[1]
//                                    alternately; final write to outputFbo
//
// Ping-pong limits memory to 2 intermediate FBOs regardless of N.
// =============================================================================

#include "effect_graph.h"
#include "../render/render_context.h"
#include "../render/render_context_additions.h"

namespace community_ar {

EffectGraph::EffectGraph(RenderContext* ctx) : ctx_(ctx) {}
EffectGraph::~EffectGraph() = default;

void EffectGraph::setEffects(std::vector<std::unique_ptr<Effect>> effects) {
    effects_ = std::move(effects);
    // The previous effects' destructors run here as effects_ is reassigned.
    // GPU resources clean up cleanly via RAII in each Effect's destructor.
}

PerceptionInputs EffectGraph::perceptionInputs() const {
    PerceptionInputs out;
    for (const auto& effect : effects_) {
        PerceptionInputs in = effect->perceptionInputs();
        out.needsFaceLandmarks |= in.needsFaceLandmarks;
        out.needsIrisLandmarks |= in.needsIrisLandmarks;
        out.needsHairMask      |= in.needsHairMask;
        out.needsSelfieMask    |= in.needsSelfieMask;
        out.needsFacePose      |= in.needsFacePose;
        out.needsSkinTone      |= in.needsSkinTone;
    }
    return out;
}

void EffectGraph::ensureChainResources(int width, int height) {
    if (chainWidth_ == width && chainHeight_ == height && chainTextures_[0]) return;
    chainWidth_ = width;
    chainHeight_ = height;
    auto* ex = static_cast<RenderContextEx*>(ctx_);
    for (int i = 0; i < 2; ++i) {
        chainTextures_[i] = ctx_->createTexture(width, height,
                                                TextureHandle::Format::RGBA8);
        chainFbos_[i] = ex->createFramebufferForTexture(*chainTextures_[i]);
    }
}

void EffectGraph::render(const TextureHandle& inputCamera,
                         const PerceptionFrame& frame,
                         Framebuffer* outputFbo) {
    // Empty graph: blit camera straight to output and bail
    if (effects_.empty()) {
        ctx_->blitTextureToFramebuffer(inputCamera, outputFbo);
        return;
    }

    // ---- Pass 1: every effect's prepare() ----
    // Mask rasterization happens here. Done up front so all effects'
    // prepare work clusters together — gives the GPU a clean barrier
    // before the dependent render work starts.
    for (auto& effect : effects_) {
        effect->prepare(frame, ctx_);
    }

    // ---- Pass 2: chained render ----
    if (effects_.size() == 1) {
        // Fast path: single effect writes directly to outputFbo
        effects_[0]->render(inputCamera, outputFbo, ctx_);
        return;
    }

    // Multi-effect path: ping-pong through chain FBOs
    ensureChainResources(frame.imageWidth, frame.imageHeight);

    int pingIdx = 0;
    const TextureHandle* readFrom = &inputCamera;

    for (size_t i = 0; i < effects_.size(); ++i) {
        bool isLast = (i == effects_.size() - 1);
        Framebuffer* writeTo = isLast ? outputFbo : chainFbos_[pingIdx].get();
        effects_[i]->render(*readFrom, writeTo, ctx_);

        if (!isLast) {
            readFrom = chainTextures_[pingIdx].get();
            pingIdx = 1 - pingIdx;   // ping-pong
        }
    }
}

}  // namespace community_ar
