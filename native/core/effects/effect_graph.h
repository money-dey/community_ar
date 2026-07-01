// effect_graph.h  (Phase 3 — replaces Phase 2 version)
// =============================================================================
// Community AR — EffectGraph v2
//
// Builds on the Phase 2 EffectGraph with three new capabilities:
//
//   1. Dependency-aware ordering. Effects sort by passOrder() before
//      running, so logical pipeline position is independent of the order
//      the user wrote them in.
//
//   2. Shared mask resources. A MaskResourcePool is populated from
//      perception output at frame start; effects consume named masks via
//      the pool instead of each rasterizing its own.
//
//   3. Pool-aware effect callbacks. New prepare() / render() overloads
//      pass the pool to effects. Phase 2 effects keep working because the
//      old overloads have default implementations that ignore the pool.
//
// Backward compatibility:
//   - Phase 2 graphs with one or more LipsEffect instances still work
//     without modification. The new sort produces the same order
//     (all Phase 2 effects default to passOrder == Recolor, stable order).
//   - The atomic setEffects() API is unchanged.
//   - perceptionInputs() union is unchanged.
//
// Lifecycle (per frame):
//
//   1. drainRenderQueue() — process pending graph mutations
//   2. maskPool_.clearForNewFrame()
//   3. maskPool_.produceFromPerception(frame) — segmenter masks land here
//   4. for each effect in sorted order:
//        effect->prepare(frame, maskPool_, ctx)
//        (effects may put() additional masks into the pool here)
//   5. for each effect in sorted order:
//        effect->render(inputTex, outputFbo, maskPool_, ctx)
//        (output of effect i becomes input of effect i+1 via ping-pong)
//
// Steps 4 and 5 stay split (matching Phase 2) so that an effect's mask
// production can be observed by later effects in the same frame.
// =============================================================================

#pragma once

#include "effect_base.h"
#include "effect_pass.h"
#include "mask_resource_pool.h"
#include <array>
#include <memory>
#include <vector>

namespace community_ar {

class TextureHandle;
class Framebuffer;
class RenderContext;

class EffectGraph {
public:
    explicit EffectGraph(RenderContext* ctx);
    ~EffectGraph();

    // Replace all effects atomically. Previous effects are destroyed
    // (RAII cleans up GPU resources). The new list is sorted internally
    // by passOrder() and assigned to effects_.
    void setEffects(std::vector<std::unique_ptr<Effect>> effects);

    // OR'd union of all effects' perception requirements.
    PerceptionInputs perceptionInputs() const;

    // OR'd union of all effects' mask requirements. Used by the perception
    // pipeline to decide which segmenter channels to compute. e.g. if no
    // effect consumes `masks.faceSkin`, the segmenter could in principle
    // skip channel splitting for it (a future optimization).
    MaskRequirements maskRequirementsUnion() const;

    // Per-frame entry point. See file header for the sequence.
    void render(const TextureHandle& inputCamera,
                const PerceptionFrame& frame,
                Framebuffer* outputFbo);

    // Diagnostic
    size_t effectCount() const { return effects_.size(); }

    // Diagnostic accessor for the mask pool, useful for the example app's
    // debug overlay.
    const MaskResourcePool& maskPool() const { return maskPool_; }

    // Look up the first installed effect with the given typeId. Returns
    // nullptr if no such effect is installed. The pointer remains valid
    // until the next setEffects() call.
    //
    // Used by the Phase 3 FFI diagnostics to query SkinSmoothEffect's
    // effective quality tier without exposing the full graph internals.
    Effect* findFirstEffectOfType(uint32_t typeId) const;

private:
    void ensureChainResources(int width, int height);
    void sortEffectsByPassOrder();

    RenderContext* ctx_;
    std::vector<std::unique_ptr<Effect>> effects_;
    MaskResourcePool maskPool_;

    // Ping-pong framebuffers for chaining (unchanged from Phase 2)
    std::array<std::unique_ptr<TextureHandle>, 2> chainTextures_;
    std::array<std::unique_ptr<Framebuffer>,   2> chainFbos_;
    int chainWidth_  = 0;
    int chainHeight_ = 0;
};

}  // namespace community_ar
