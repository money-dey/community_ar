// perception_pipeline.h
// =============================================================================
// Community AR — Perception pipeline
//
// Owns the neural perception models (FaceMesh, Iris, hair seg, etc.) and
// runs them per frame. Driven by the union of all enabled effects'
// PerceptionInputs — models whose outputs aren't needed are not run.
//
// Architecture:
//   - One pipeline instance per session
//   - Runs entirely on the render thread (no internal threading)
//   - Models loaded lazily on first use; idle models can be unloaded
//     under memory pressure
//   - All landmark output passes through OneEuroFilter for temporal stability
// =============================================================================

#pragma once

#include "perception_frame.h"
#include <memory>
#include <string>

namespace community_ar {

class NeuralBackend;     // abstract; TFLite on Android, Core ML on iOS
class OneEuroFilter;
class TextureHandle;
class RenderContext;
struct SegmenterBackendConfig;   // segmenter_backend_factory.h

// -----------------------------------------------------------------------------
// PerceptionInputs — what an effect needs from the perception layer.
//
// Each effect declares its requirements via Effect::perceptionInputs(); the
// EffectGraph ORs them into a union and hands the result to
// setRequirements(). Models whose outputs are not requested are never run
// (perception-on-demand, CLAUDE.md invariant 3).
//
// (Historically this lived in a separate effects/effect.h; consolidated here
// so perception has no dependency on the effects layer. The Phase 3 segmenter
// flags were merged in from perception_inputs_phase3_updates.h.)
// -----------------------------------------------------------------------------
struct PerceptionInputs {
    // === Phase 1/2 fields ===
    bool needsFaceLandmarks = false;
    bool needsIrisLandmarks = false;
    bool needsHairMask      = false;
    bool needsSelfieMask    = false;
    bool needsFacePose      = false;
    bool needsSkinTone      = false;

    // === Phase 3 additions (multiclass segmenter channels) ===

    // Beauty v2 consumes this. The multiclass segmenter populates the
    // face-skin channel which lands in maskPool[kFaceSkin].
    bool needsFaceSkinMask     = false;

    // Plumbed now for Phase 6 background / clothing effects; no effect
    // consumes these in Phase 3 itself.
    bool needsBodySkinMask     = false;
    bool needsClothesMask      = false;
    bool needsBackgroundMask   = false;
};

struct PerceptionConfig {
    std::string modelDirectory;
    int maxFaces;
    bool enableGpuInference;
    bool enableNpuInference;
};

class PerceptionPipeline {
public:
    // The RenderContext is required for GPU-side perception work (skin tone
    // compute, segmenter output textures). It is captured at construction and
    // must outlive the pipeline.
    PerceptionPipeline(const PerceptionConfig& cfg,
                       NeuralBackend* backend,
                       RenderContext* ctx);
    ~PerceptionPipeline();

    // Choose and create the segmentation backend (multiclass with hair-only
    // fallback). Called once, after construction, from the session setup path
    // once model-file availability is known.
    void initSegmenterBackend(NeuralBackend* neuralBackend,
                              const SegmenterBackendConfig& cfg);

    // Declare what perception data is needed this frame.
    // Called once per frame, before run(), based on the active composition.
    void setRequirements(const PerceptionInputs& inputs);

    // Run all enabled perception steps for the given camera frame.
    // The returned PerceptionFrame is owned by this pipeline and remains
    // valid until the next call to run().
    const PerceptionFrame& run(const TextureHandle& cameraTexture,
                               int64_t captureTimestampNs);

    // Memory management
    void unloadIdleModels();  // unload models not used for N frames

private:
    // Per-frame segmenter execution; populates outFrame.segmentationMasks
    // (and the compatibility outFrame.hairMask). Called from run().
    void runSegmenterForFrame(const TextureHandle& cameraTex,
                              PerceptionFrame& outFrame,
                              RenderContext* ctx);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace community_ar
