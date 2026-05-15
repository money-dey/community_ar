// effect_graph.h
// =============================================================================
// Community AR — EffectGraph (minimal v1)
//
// Owns a list of Effect instances and runs them in declared order per frame.
// Maintains a pair of ping-pong framebuffers so each effect reads the
// previous stage's output and writes to a fresh FBO.
//
// API contract:
//   - The graph is REPLACED atomically via setEffects(). Mid-frame edits
//     are not supported.
//   - The graph contributes its PerceptionInputs union to the Session,
//     which configures the PerceptionPipeline accordingly.
//   - render() is the per-frame entry point. It runs all prepare()s, then
//     all render()s, writing the final result to outputFbo.
//
// Phase 2 limitations (deferred to Phase 3):
//   - No dependency analysis: effects always run in declared order
//   - No parallel execution: effects run sequentially
//   - No shared mask resources: each effect rasterizes its own mask
//   - No per-effect config updates: changing one effect = rebuild whole graph
// =============================================================================

#pragma once

#include "effect_base.h"
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
    // (their destructors clean up GPU resources). New effects' GPU
    // resources are allocated lazily on first prepare().
    void setEffects(std::vector<std::unique_ptr<Effect>> effects);

    // Returns the OR'd union of all effects' perception requirements.
    // The Session calls this after setEffects() to reconfigure the
    // PerceptionPipeline.
    PerceptionInputs perceptionInputs() const;

    // Per-frame entry point. Runs every effect's prepare() then render().
    //
    // inputCamera: the camera output texture (Phase 0's pipeline result).
    // frame:       the latest PerceptionFrame from PerceptionPipeline.
    // outputFbo:   where the final composited result lands. The Session
    //              points this at the display texture that Flutter sees.
    void render(const TextureHandle& inputCamera,
                const PerceptionFrame& frame,
                Framebuffer* outputFbo);

    // Diagnostic
    size_t effectCount() const { return effects_.size(); }

private:
    void ensureChainResources(int width, int height);

    RenderContext* ctx_;
    std::vector<std::unique_ptr<Effect>> effects_;

    // Ping-pong framebuffers for chaining effects. With N effects we need
    // N-1 intermediate FBOs (effect 0 writes to chain[0], effect 1 reads
    // chain[0] writes to chain[1], etc.). For Phase 2's typical N=1 case
    // we don't even use these — effect 0 writes directly to outputFbo.
    std::array<std::unique_ptr<TextureHandle>, 2> chainTextures_;
    std::array<std::unique_ptr<Framebuffer>, 2>   chainFbos_;

    int chainWidth_  = 0;
    int chainHeight_ = 0;
};

}  // namespace community_ar
