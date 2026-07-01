// effect_graph.cpp  (Phase 3 — replaces Phase 2 version)
// =============================================================================
// EffectGraph v2 implementation.
//
// The render path:
//
//   render(inputCamera, frame, outputFbo)
//     ├─ maskPool_.clearForNewFrame()
//     ├─ maskPool_.produceFromPerception(frame)
//     │     ↳ segmenter masks land in the pool under canonical names
//     │
//     ├─ for each effect (in passOrder):
//     │     effect->prepare(frame, maskPool_, ctx_)
//     │       ↳ effect may put() additional masks (e.g. lip contour)
//     │       ↳ effect may pre-compute internal state
//     │
//     └─ for each effect (in passOrder):
//           inputTex = previous effect's output (or inputCamera for first)
//           outputFbo' = next chain FBO (or outputFbo for last)
//           effect->render(inputTex, outputFbo', maskPool_, ctx_)
// =============================================================================

#include "effect_graph.h"
#include "../render/render_context.h"
#include "../render/render_context_additions.h"
#include <algorithm>

namespace community_ar {

// -----------------------------------------------------------------------------
// Construction / destruction
// -----------------------------------------------------------------------------
EffectGraph::EffectGraph(RenderContext* ctx) : ctx_(ctx) {}
EffectGraph::~EffectGraph() = default;

// -----------------------------------------------------------------------------
// setEffects: replace the entire list atomically, sort by passOrder.
//
// Old effects' destructors run as effects_ is reassigned; their GPU
// resources clean up via RAII inside each Effect's destructor.
// -----------------------------------------------------------------------------
void EffectGraph::setEffects(std::vector<std::unique_ptr<Effect>> effects) {
    effects_ = std::move(effects);
    sortEffectsByPassOrder();
}

void EffectGraph::sortEffectsByPassOrder() {
    // stable_sort preserves declaration order among effects with the same
    // passOrder() value. This matches the documented contract: equal-
    // priority effects run in the order the user passed them in.
    std::stable_sort(effects_.begin(), effects_.end(),
                     [](const std::unique_ptr<Effect>& a,
                        const std::unique_ptr<Effect>& b) {
                         return passOrderValue(a->passOrder())
                              < passOrderValue(b->passOrder());
                     });
}

// -----------------------------------------------------------------------------
// Perception requirements union
// -----------------------------------------------------------------------------
PerceptionInputs EffectGraph::perceptionInputs() const {
    PerceptionInputs out;
    for (const auto& effect : effects_) {
        PerceptionInputs in = effect->perceptionInputs();
        out.needsFaceLandmarks   |= in.needsFaceLandmarks;
        out.needsIrisLandmarks   |= in.needsIrisLandmarks;
        out.needsHairMask        |= in.needsHairMask;
        out.needsSelfieMask      |= in.needsSelfieMask;
        out.needsFacePose        |= in.needsFacePose;
        out.needsSkinTone        |= in.needsSkinTone;
        // Phase 3 additions
        out.needsFaceSkinMask    |= in.needsFaceSkinMask;
        out.needsBodySkinMask    |= in.needsBodySkinMask;
        out.needsClothesMask     |= in.needsClothesMask;
        out.needsBackgroundMask  |= in.needsBackgroundMask;
    }
    return out;
}

// -----------------------------------------------------------------------------
// findFirstEffectOfType — linear scan, returns first match or nullptr
//
// Used by the Phase 3 FFI to query a specific effect's diagnostic state
// (e.g. SkinSmoothEffect::effectiveQuality()). N is tiny (~3-10 effects)
// so linear is fine.
// -----------------------------------------------------------------------------
Effect* EffectGraph::findFirstEffectOfType(uint32_t typeId) const {
    for (const auto& effect : effects_) {
        if (effect->typeId() == typeId) return effect.get();
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Mask requirements union
// -----------------------------------------------------------------------------
MaskRequirements EffectGraph::maskRequirementsUnion() const {
    MaskRequirements out;
    // Collect unique consumes and produces across all effects.
    // Tiny N (typically < 10 effects), so the O(N²) dedup is fine.
    auto pushUnique = [](std::vector<std::string>& vec,
                         const std::string& s) {
        if (std::find(vec.begin(), vec.end(), s) == vec.end()) vec.push_back(s);
    };
    for (const auto& effect : effects_) {
        auto req = effect->maskRequirements();
        for (const auto& c : req.consumes) pushUnique(out.consumes, c);
        for (const auto& p : req.produces) pushUnique(out.produces, p);
    }
    return out;
}

// -----------------------------------------------------------------------------
// Ping-pong chain FBOs (unchanged from Phase 2)
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// render — the per-frame entry point
// -----------------------------------------------------------------------------
void EffectGraph::render(const TextureHandle& inputCamera,
                         const PerceptionFrame& frame,
                         Framebuffer* outputFbo) {
    // Empty graph: blit camera to output and bail
    if (effects_.empty()) {
        ctx_->blitTextureToFramebuffer(inputCamera, outputFbo);
        return;
    }

    // ---- Step 1: refresh the mask pool ----
    maskPool_.clearForNewFrame();
    maskPool_.produceFromPerception(frame);

    // ---- Step 2: prepare phase ----
    // Effects may produce additional masks (e.g. landmark contours) here.
    // We run all prepares before any renders so producers go before consumers
    // even if a later-passOrder effect produces a mask that an earlier one
    // would normally need. (In practice this is rare — most consumers are
    // later than their producers in passOrder anyway — but the split lets
    // us not constrain that.)
    for (auto& effect : effects_) {
        effect->prepare(frame, maskPool_, ctx_);
    }

    // ---- Step 3: render phase ----
    if (effects_.size() == 1) {
        // Single-effect fast path — write directly to outputFbo
        effects_[0]->render(inputCamera, outputFbo, maskPool_, ctx_);
        return;
    }

    ensureChainResources(frame.imageWidth, frame.imageHeight);

    int pingIdx = 0;
    const TextureHandle* readFrom = &inputCamera;

    for (size_t i = 0; i < effects_.size(); ++i) {
        bool isLast = (i == effects_.size() - 1);
        Framebuffer* writeTo = isLast ? outputFbo : chainFbos_[pingIdx].get();

        effects_[i]->render(*readFrom, writeTo, maskPool_, ctx_);

        if (!isLast) {
            readFrom = chainTextures_[pingIdx].get();
            pingIdx = 1 - pingIdx;  // ping-pong
        }
    }
}

}  // namespace community_ar
