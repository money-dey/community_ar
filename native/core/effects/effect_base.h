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
#include <cstdint>
#include <memory>

namespace community_ar {

class TextureHandle;
class Framebuffer;
class RenderContext;

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

    // Called once per frame, BEFORE render(). Use this to:
    //   - Rasterize any masks the effect needs
    //   - Update internal per-frame state (e.g. animation t parameter)
    //   - Allocate / resize GPU resources to match the current frame size
    virtual void prepare(const PerceptionFrame& frame,
                         RenderContext* ctx) = 0;

    // Called once per frame, AFTER prepare(). The effect reads inputTex
    // (the previous stage's output, or the camera frame for the first
    // effect) and writes its composited result to outputFbo.
    //
    // Effects must NOT modify inputTex. The graph guarantees a fresh
    // output framebuffer; the effect can clear it or sample inputTex
    // as the base layer.
    virtual void render(const TextureHandle& inputTex,
                        Framebuffer* outputFbo,
                        RenderContext* ctx) = 0;
};

}  // namespace community_ar
