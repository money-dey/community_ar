// phase0_session_phase2_updates.cpp
// =============================================================================
// Phase0Session updates for Phase 2 integration.
//
// Adds:
//   - effectGraph_ member that owns the active effects
//   - Per-frame call sequence: PerceptionPipeline → EffectGraph → display
//   - perceptionPipeline() / effectGraph() accessors
//   - runOnRenderThread() helper used by the C ABI
//
// Replaces:
//   - The Phase 0 test-shader rendering in renderFrame() is now bypassed
//     when an effect graph is installed.
//
// Does NOT modify:
//   - Camera input plumbing (texture handle remains the same)
//   - Output texture handed to Flutter (Phase 0 path unchanged)
// =============================================================================

#include "phase0_session.h"
#include "effects/effect_graph.h"
#include "perception/perception_pipeline.h"
#include "perception/segmenter_backend_factory.h"  // SegmenterBackendConfig
#include <mutex>
#include <queue>

namespace community_ar {

// ----------------------------------------------------------------------------
// Phase0Session::Impl additions
// (Conceptual — the actual diff against the existing Impl struct adds these
// members to the existing one rather than declaring a new Impl.)
// ----------------------------------------------------------------------------
struct Phase0Session::Phase2Members {
    std::unique_ptr<EffectGraph>        effectGraph;
    std::unique_ptr<PerceptionPipeline> perceptionPipeline;

    // Render-thread work queue. Public ABI calls push lambdas here; the
    // render thread drains the queue at the top of each frame.
    std::mutex                                 queueMutex;
    std::queue<std::function<void()>>          pending;
};

// ----------------------------------------------------------------------------
// Accessors used by the Phase 2 ABI
// ----------------------------------------------------------------------------
EffectGraph& Phase0Session::effectGraph() {
    if (!p2_->effectGraph) {
        p2_->effectGraph = std::make_unique<EffectGraph>(renderContext());
    }
    return *p2_->effectGraph;
}

PerceptionPipeline& Phase0Session::perceptionPipeline() {
    if (!p2_->perceptionPipeline) {
        PerceptionConfig pcfg;
        pcfg.maxFaces = 4;
        p2_->perceptionPipeline = std::make_unique<PerceptionPipeline>(
            pcfg, neuralBackend(), renderContext());
        // Choose the segmentation backend (default: multiclass with hair-only
        // fallback). Without this the pipeline runs no segmenter and produces
        // no mask channels.
        p2_->perceptionPipeline->initSegmenterBackend(
            neuralBackend(), SegmenterBackendConfig{});
    }
    return *p2_->perceptionPipeline;
}

// ----------------------------------------------------------------------------
// runOnRenderThread — queue a lambda to execute on the render thread before
// the next frame begins. Used by the Phase 2 ABI to make graph mutations
// thread-safe without exposing any locks.
// ----------------------------------------------------------------------------
void Phase0Session::runOnRenderThread(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(p2_->queueMutex);
    p2_->pending.push(std::move(task));
}

void Phase0Session::drainRenderQueue() {
    // Move tasks out under the lock, execute outside it (so a task can
    // enqueue more without deadlocking).
    std::queue<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(p2_->queueMutex);
        std::swap(local, p2_->pending);
    }
    while (!local.empty()) {
        local.front()();
        local.pop();
    }
}

// ----------------------------------------------------------------------------
// Per-frame render path with Phase 2 integration.
//
// This replaces Phase 0's renderFrame() when an effect graph is installed.
// Phase 0's path is preserved for the empty-graph case (so the Phase 0
// test shader UI still works during development).
// ----------------------------------------------------------------------------
void Phase0Session::renderFramePhase2(int64_t captureTimestampNs) {
    // 1. Drain queued ABI work (graph swaps, etc.) before rendering
    drainRenderQueue();

    // 2. Run perception. The pipeline already knows its requirements
    //    (set by car_p2_graph_set / car_p2_graph_clear).
    const PerceptionFrame& frame =
        perceptionPipeline().run(cameraOutputTexture(), captureTimestampNs);

    // 3. Run the effect graph. If empty, this just blits camera → display.
    effectGraph().render(cameraOutputTexture(), frame, displayFramebuffer());

    // 4. Phase 0's existing post-render notifications (Flutter texture
    //    available signal, etc.) continue from here.
}

}  // namespace community_ar
