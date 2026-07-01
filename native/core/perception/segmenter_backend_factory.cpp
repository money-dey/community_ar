// segmenter_backend_factory.cpp
// =============================================================================
// Factory implementation.
//
// The fallback chain: requested → known-good → none. We try the preferred
// backend first, but if its model file is missing or it fails to construct,
// we transparently drop to the hair segmenter (Phase 1 default). If that
// also fails, we return nullptr.
//
// The caller (perception pipeline) gets a working backend or null; it
// never has to handle "partial init" states.
// =============================================================================

#include "segmenter_backend_factory.h"
#include "multiclass_segmenter_backend.h"
#include "hair_segmenter_backend.h"
#include <fstream>
#include <iostream>

namespace community_ar {

namespace {
// Helper: check if a file exists at the given path
bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// Helper: join directory and filename with '/' separator
std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    if (dir.back() == '/') return dir + file;
    return dir + "/" + file;
}
}  // anonymous

std::unique_ptr<SegmenterBackend> createSegmenterBackend(
        NeuralBackend* neuralBackend,
        const SegmenterBackendConfig& config) {
    if (!neuralBackend) return nullptr;
    if (config.preferredBackend == "none") return nullptr;

    // Try the preferred backend first.
    if (config.preferredBackend == "multiclass") {
        std::string path = joinPath(config.modelDirectory,
                                     "selfie_multiclass_256x256.tflite");
        if (!config.verifyModelFileExists || fileExists(path)) {
            // Construction itself is cheap (no I/O until first run()), so we
            // construct now and let lazy loading happen on demand.
            return std::make_unique<MulticlassSegmenterBackend>(neuralBackend, path);
        }
        // File missing: fall through to hair segmenter fallback
        std::cerr << "[CommunityAR] Multiclass model not found at " << path
                  << "; falling back to hair_segmenter.tflite\n";
    }

    // Hair segmenter fallback (or explicit "hair" preference)
    std::string hairPath = joinPath(config.modelDirectory,
                                     "hair_segmenter.tflite");
    if (!config.verifyModelFileExists || fileExists(hairPath)) {
        return std::make_unique<HairSegmenterBackend>(neuralBackend, hairPath);
    }

    // No segmenter available.
    std::cerr << "[CommunityAR] No segmenter model files found in "
              << config.modelDirectory
              << "; segmentation-dependent effects will be disabled\n";
    return nullptr;
}

}  // namespace community_ar
