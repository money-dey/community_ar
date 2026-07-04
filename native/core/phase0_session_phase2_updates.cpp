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

CARStatus Phase0Session::setModelDirectory(const char* dir) {
    if (!dir) return CAR_STATUS_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(p2_->cfgMutex);
    p2_->modelDirectory = dir;
    return CAR_STATUS_OK;
}

void Phase0Session::getPerceptionStatsSnapshot(
        CARPerceptionStats* outStats) const {
    if (!outStats) return;
    outStats->facesDetected =
        p2_->statFacesDetected.load(std::memory_order_relaxed);
    outStats->skinBaselineLuma =
        p2_->statSkinLuma.load(std::memory_order_relaxed);
    outStats->skinToneValid =
        p2_->statSkinValid.load(std::memory_order_relaxed);
    // Per-model inference times / filter counts: not yet surfaced by the
    // pipeline — read as 0 until WP-E plumbs them through.
}

void Phase0Session::setDebugOverlayMask(uint32_t modeMask) {
    p2_->debugOverlayMask.store(modeMask, std::memory_order_relaxed);
}

void Phase0Session::setForcedPerceptionBits(uint32_t bits) {
    p2_->forcedPerceptionBits.store(bits, std::memory_order_relaxed);
}

NeuralBackend* Phase0Session::neuralBackend() {
    if (!p2_->neuralBackend) {
        BackendConfig bc;
        bc.renderContext = ctx_.get();
        // Supplied by the platform via car_p0_set_model_directory (Android:
        // filesDir/models after first-launch asset extraction). If it's still
        // empty here, model loads fail with a clear per-file log in the
        // backend — perception stays inactive rather than crashing.
        {
            std::lock_guard<std::mutex> lock(p2_->cfgMutex);
            bc.modelDirectory = p2_->modelDirectory;
        }
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

size_t Phase0Session::installedEffectCount() const {
    // Pointer-check only — never construct from here (this is callable from
    // the platform thread; construction belongs to the render thread).
    return p2_->effectGraph ? p2_->effectGraph->effectCount() : 0;
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

    // 2. Recompute perception requirements: the union of what the installed
    //    effects need, plus anything force-enabled for debugging (overlays
    //    must have data to draw when no effect requested that model).
    PerceptionInputs req = effectGraph().perceptionInputs();
    const uint32_t forced =
        p2_->forcedPerceptionBits.load(std::memory_order_relaxed);
    if (forced) {
        req.needsFaceLandmarks |= (forced & kForceFaceLandmarks) != 0;
        req.needsIrisLandmarks |= (forced & kForceIris) != 0;
        req.needsHairMask      |= (forced & kForceHair) != 0;
        req.needsSelfieMask    |= (forced & kForceSelfieSeg) != 0;
        req.needsFacePose      |= (forced & kForcePose) != 0;
        req.needsSkinTone      |= (forced & kForceSkinTone) != 0;
    }
    perceptionPipeline().setRequirements(req);
    const PerceptionFrame& frame =
        perceptionPipeline().run(cameraOutputTexture(), captureTimestampNs);

    // Publish the stats snapshot for car_p1_get_perception_stats (read from
    // the platform channel thread; see Phase2Members).
    p2_->statFacesDetected.store((int)frame.faces.size(),
                                 std::memory_order_relaxed);
    if (!frame.faces.empty()) {
        const auto& tone = frame.faces.front().skinTone;
        p2_->statSkinLuma.store(tone.valid ? tone.baselineLuma : 0.0f,
                                std::memory_order_relaxed);
        p2_->statSkinValid.store(tone.valid ? 1 : 0, std::memory_order_relaxed);
    } else {
        p2_->statSkinValid.store(0, std::memory_order_relaxed);
    }

    // 3. Run the effect graph. If empty, this just blits camera → display.
    effectGraph().render(cameraOutputTexture(), frame, displayFramebuffer());

    // 3.5 Debug overlay (WP-E): composite perception visualizations over the
    //     effect output, still in the offscreen FBO so the present blit picks
    //     it up. Free when the mask is 0.
    const uint32_t dbgMask =
        p2_->debugOverlayMask.load(std::memory_order_relaxed);
    if (dbgMask) {
        if (!p2_->debugOverlay) {
            p2_->debugOverlay = std::make_unique<DebugOverlay>(renderContext());
        }
        p2_->debugOverlay->setMode(dbgMask);
        p2_->debugOverlay->render(cameraOutputTexture(), frame,
                                  displayFramebuffer());
    }

    // 4. Phase 0's existing post-render notifications (Flutter texture
    //    available signal, etc.) continue from here.
}

}  // namespace community_ar
