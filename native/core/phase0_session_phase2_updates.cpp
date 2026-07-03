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
#include "phase0_session_phase2.h"  // Phase2Members definition
#include "effects/effect_graph.h"
#include "perception/perception_pipeline.h"
#include "perception/segmenter_backend_factory.h"  // SegmenterBackendConfig
#include "ml/neural_backend.h"                      // BackendConfig + factories
#include <mutex>
#include <queue>

namespace community_ar {

// Phase2Members is defined in phase0_session_phase2.h so both this file and
// phase0_session.cpp (which owns p2_'s lifetime) share one layout.

// ----------------------------------------------------------------------------
// Backing-object accessors
// ----------------------------------------------------------------------------
RenderContext* Phase0Session::renderContext() {
    return ctx_.get();
}

NeuralBackend* Phase0Session::neuralBackend() {
    if (!p2_->neuralBackend) {
        BackendConfig bc;
        bc.renderContext = ctx_.get();
        // NOTE: CARPhase0Config carries no model directory, so bc.modelDirectory
        // is left empty here. Device bring-up must supply the model path before
        // any model can actually load — see the session-integration follow-up.
#if defined(__ANDROID__)
        p2_->neuralBackend = createTfliteBackend(bc);
#elif defined(__APPLE__)
        p2_->neuralBackend = createCoreMLBackend(bc);
#endif
    }
    return p2_->neuralBackend.get();
}

const TextureHandle& Phase0Session::cameraOutputTexture() {
    // WP-B: the AR submit path (processFrameAR) ingress-copies the camera OES
    // frame into a 2D texture each frame, with the UV transform applied —
    // perception + effects sample that. Before the first ingress (or on the
    // legacy offscreen path) fall back to an invalid handle so the path stays
    // type-correct.
    if (ingressFbo_) return ingressFbo_->colorTexture();
    if (!p2_->cameraTex) {
        p2_->cameraTex = std::make_unique<TextureHandle>();
    }
    return *p2_->cameraTex;
}

Framebuffer* Phase0Session::displayFramebuffer() {
    return outputFbo_.get();
}

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
