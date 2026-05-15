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
#include "../effects/effect.h"   // for PerceptionInputs
#include <memory>
#include <string>

namespace community_ar {

class NeuralBackend;     // abstract; TFLite on Android, Core ML on iOS
class OneEuroFilter;
class TextureHandle;

struct PerceptionConfig {
    std::string modelDirectory;
    int maxFaces;
    bool enableGpuInference;
    bool enableNpuInference;
};

class PerceptionPipeline {
public:
    explicit PerceptionPipeline(const PerceptionConfig& cfg,
                                NeuralBackend* backend);
    ~PerceptionPipeline();

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
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace community_ar
