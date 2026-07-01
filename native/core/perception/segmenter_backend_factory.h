// segmenter_backend_factory.h
// =============================================================================
// SegmenterBackend factory
//
// Picks which SegmenterBackend implementation to use based on session
// configuration. Encapsulates the model-path-resolution logic that
// shouldn't pollute the perception pipeline.
//
// The selection logic is:
//   1. If config.preferredBackend is explicit, try that one.
//   2. If "multiclass" is requested and the model file exists, use it.
//   3. Otherwise fall back to the hair segmenter.
//   4. If even the hair model is missing, return nullptr (perception pipeline
//      handles this by skipping segmentation-dependent effects).
//
// This is also where M7 verification gating could happen if we wanted to
// gate the multiclass model behind a feature flag. For Phase 3 launch,
// we keep it as a simple "try preferred, fall back to known-good."
// =============================================================================

#pragma once

#include "segmenter_backend.h"
#include <memory>
#include <string>

namespace community_ar {

class NeuralBackend;

struct SegmenterBackendConfig {
    // Directory containing the model files. Used to construct full paths.
    std::string modelDirectory;

    // Which backend to prefer:
    //   "multiclass"  — try multiclass first, fall back to hair (default)
    //   "hair"        — use hair segmenter only
    //   "none"        — skip segmentation entirely
    std::string preferredBackend = "multiclass";

    // If true, the factory verifies the model file exists before
    // returning the backend. If false, the backend may fail at first
    // run() instead. Use true unless you have a reason to defer the check.
    bool verifyModelFileExists = true;
};

// Create a backend matching the requested config. Returns nullptr if no
// backend could be created (e.g. all model files missing).
//
// The returned backend takes ownership of nothing; the neural backend
// pointer must remain valid for the lifetime of the returned object.
std::unique_ptr<SegmenterBackend> createSegmenterBackend(
    NeuralBackend* neuralBackend,
    const SegmenterBackendConfig& config);

}  // namespace community_ar
