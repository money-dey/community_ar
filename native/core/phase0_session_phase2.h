// phase0_session_phase2.h
// =============================================================================
// Internal definition of Phase0Session::Phase2Members.
//
// PRIVATE header, included only by the Phase0Session translation units
// (phase0_session.cpp, which owns the p2_ member's lifetime, and
// phase0_session_phase2_updates.cpp, which defines the Phase 2 accessors).
//
// Before consolidation the Phase 2 additions (effect graph, perception
// pipeline, neural backend, render-thread queue) were assumed by the delta file
// but never actually declared on Phase0Session, and Phase2Members was defined
// locally in a single .cpp — so p2_ had no visible type at the destructor.
// Centralising the struct here fixes that.
// =============================================================================

#pragma once

#include "phase0_session.h"
#include "effects/effect_graph.h"
#include "perception/perception_pipeline.h"
#include "ml/neural_backend.h"
#include "render/render_context.h"
#include "render/debug_overlay.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>

namespace community_ar {

struct Phase0Session::Phase2Members {
    std::unique_ptr<EffectGraph>        effectGraph;
    std::unique_ptr<PerceptionPipeline> perceptionPipeline;

    // The session owns the neural backend (TFLite/Core ML). Created lazily on
    // first perception use.
    std::unique_ptr<NeuralBackend>      neuralBackend;

    // Latest camera frame as a sampleable handle, consumed by the Phase 2
    // render path. Populated by the frame-submit path (see note in
    // cameraOutputTexture()).
    std::unique_ptr<TextureHandle>      cameraTex;

    // Render-thread work queue. Public ABI calls push lambdas here; the render
    // thread drains the queue at the top of each frame.
    std::mutex                          queueMutex;
    std::queue<std::function<void()>>   pending;

    // Model directory (car_p0_set_model_directory). Written from any thread
    // before perception first runs; read once when the neural backend is
    // lazily created on the render thread.
    std::mutex                          cfgMutex;
    std::string                         modelDirectory;

    // Perception stats snapshot — written by the render thread each AR frame,
    // read by the platform thread (car_p1_get_perception_stats).
    std::atomic<int>   statFacesDetected{0};
    std::atomic<float> statSkinLuma{0.0f};
    std::atomic<int>   statSkinValid{0};

    // ---- WP-E debug state ----
    // Written from the platform thread (car_p1_set_debug_overlay /
    // car_p1_force_perception), read by the render thread each AR frame.
    // Atomics rather than the render queue: these are latest-wins values with
    // no ordering relationship to graph mutations.
    std::atomic<uint32_t> debugOverlayMask{0};
    std::atomic<uint32_t> forcedPerceptionBits{0};   // kForce* (phase0_session.h)

    // Lazily created on the render thread the first time the mask is nonzero.
    std::unique_ptr<DebugOverlay> debugOverlay;
};

}  // namespace community_ar
