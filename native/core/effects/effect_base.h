// effect_base.h
// =============================================================================
// Community AR — Abstract Effect interface
//
// Every effect in the library implements this interface. The EffectGraph
// orchestrator calls these methods in a strict order per frame:
//
//   1. perceptionInputs()   — once per graph rebuild, contributes to the
//                              union that PerceptionPipeline computes
//   2. prepare(frame, ctx)  — once per frame, rasterize masks or compute
//                              per-frame state
//   3. render(input, fbo)   — once per frame, sample the previous stage's
//                              output and write composited result
//
// The split between prepare() and render() exists because Phase 3+ will
// allow effects to share mask resources — prepare() declares what masks
// the effect produces; render() consumes the deduplicated mask pool.
// In Phase 2 each effect simply prepares its own mask.
// =============================================================================

#pragma once

#include "../perception/perception_frame.h"
#include "../perception/perception_pipeline.h"
#include "effect_pass.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace community_ar {

class TextureHandle;
class Framebuffer;
class RenderContext;
class MaskResourcePool;

// -----------------------------------------------------------------------------
// MaskRequirements — mask resources an effect reads and/or writes.
//
// Parallel to PerceptionInputs (what an effect needs from perception); this
// describes what it needs from the per-frame MaskResourcePool.
//   - consumes: names of masks that must be populated before the effect runs
//   - produces: names of masks this effect makes available to later effects
// Namespace-scope (not nested in Effect) for symmetry with PerceptionInputs.
// -----------------------------------------------------------------------------
struct MaskRequirements {
    std::vector<std::string> consumes;
    std::vector<std::string> produces;
};

// -----------------------------------------------------------------------------
// Effect — the abstract base class
//
// Lifetime contract:
//   - Constructed when EffectGraph::setEffects() is called (off the render
//     thread is fine; no GPU work in the constructor).
//   - First prepare() / render() call happens on the render thread. GPU
//     resources should be lazily created on first prepare().
//   - Destructor runs when the graph is replaced. Must clean up GPU
//     resources cleanly.
//
// Thread safety:
//   - prepare() and render() are always called on the render thread.
//   - perceptionInputs() may be called on any thread; must be pure / const.
//   - typeId() is pure / const.
// -----------------------------------------------------------------------------
class Effect {
public:
    virtual ~Effect() = default;

    // Stability-bound numeric ID for this effect type. See effect_types.h
    // for the registry.
    virtual uint32_t typeId() const = 0;

    // What this effect needs from the perception pipeline. The EffectGraph
    // ORs these together and configures PerceptionPipeline accordingly.
    virtual PerceptionInputs perceptionInputs() const = 0;

    // Pass-order category for graph sorting. The graph runs effects in
    // ascending order of this value, stable within the same value. Default
    // Recolor matches the pipeline position of the Phase 2 effects.
    virtual EffectPass passOrder() const { return EffectPass::Recolor; }

    // Mask resources this effect reads and/or writes. Default: empty for
    // both — Phase 2 effects rasterize their own masks internally and don't
    // share them via the pool.
    virtual MaskRequirements maskRequirements() const { return {}; }

    // Called once per frame, BEFORE render(). Use this to:
    //   - Rasterize any masks the effect needs
    //   - Update internal per-frame state (e.g. animation t parameter)
    //   - Allocate / resize GPU resources to match the current frame size
    //
    // Non-pure: effects that need the mask pool override the pool-receiving
    // overload below instead. Effects that don't (Phase 2 recolor) override
    // this one.
    virtual void prepare(const PerceptionFrame& frame,
                         RenderContext* ctx) {}

    // Called once per frame, AFTER prepare(). The effect reads inputTex
    // (the previous stage's output, or the camera frame for the first
    // effect) and writes its composited result to outputFbo.
    //
    // Effects must NOT modify inputTex. The graph guarantees a fresh
    // output framebuffer; the effect can clear it or sample inputTex
    // as the base layer.
    virtual void render(const TextureHandle& inputTex,
                        Framebuffer* outputFbo,
                        RenderContext* ctx) {}

    // ---- Phase 3 pool-aware overloads ----
    // The EffectGraph always calls these. The default implementations
    // delegate to the pool-free versions above, so a Phase 2 effect that
    // overrides only prepare(frame, ctx) / render(input, fbo, ctx) keeps
    // working unchanged. Effects that need the shared MaskResourcePool
    // (e.g. SkinSmoothEffect) override these directly.
    virtual void prepare(const PerceptionFrame& frame,
                         MaskResourcePool& maskPool,
                         RenderContext* ctx) {
        (void)maskPool;
        prepare(frame, ctx);
    }

    virtual void render(const TextureHandle& inputTex,
                        Framebuffer* outputFbo,
                        const MaskResourcePool& maskPool,
                        RenderContext* ctx) {
        (void)maskPool;
        render(inputTex, outputFbo, ctx);
    }
};

}  // namespace community_ar
