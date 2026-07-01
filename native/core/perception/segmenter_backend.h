// segmenter_backend.h
// =============================================================================
// Community AR — SegmenterBackend interface
//
// Abstracts over the choice of which on-device segmentation model the
// perception pipeline uses. Two concrete implementations:
//
//   1. HairSegmenterBackend       — uses hair_segmenter.tflite (~5MB),
//                                   produces only the hair channel.
//                                   The Phase 1 default.
//
//   2. MulticlassSegmenterBackend — uses selfie_multiclass_256x256.tflite
//                                   (~15.6MB), produces 6 channels:
//                                   background, hair, body-skin, face-skin,
//                                   clothes, others. The Phase 3 default.
//
// The pipeline picks one backend at session creation time; downstream
// code (effects, mask pool, rendering) doesn't know or care which is in
// use. Channels the chosen backend doesn't produce return null textures —
// callers handle that gracefully (typically by falling back to landmark-
// derived alternatives).
//
// Verification status (May 2026):
//   - Interface: designed but not yet exercised on real hardware
//   - HairSegmenterBackend: wraps existing Phase 1 code
//   - MulticlassSegmenterBackend: new code, not benchmarked on Snapdragon
//     7-class or A12+ devices. Phase 3 verification gate (M7 in the
//     car-phase-3-dag.md) must run before this backend is treated as default
//     in production builds.
// =============================================================================

#pragma once

#include "../ml/neural_backend.h"
#include "../render/render_context.h"
#include <memory>
#include <string>

namespace community_ar {

// -----------------------------------------------------------------------------
// SegmentationChannels
//
// Output container for a segmenter inference. Each field is a GPU texture
// handle holding a single-channel R8 mask in [0, 1] where 1.0 means
// "this pixel belongs to this class." Channels the backend doesn't
// produce are nullptr.
//
// All textures are sized to the segmenter's native output resolution
// (256x256 for both supported backends as of Phase 3). Effects that need
// other resolutions resample at consumption time.
// -----------------------------------------------------------------------------
struct SegmentationChannels {
    std::shared_ptr<TextureHandle> background;   // multiclass only
    std::shared_ptr<TextureHandle> hair;         // both backends
    std::shared_ptr<TextureHandle> bodySkin;     // multiclass only
    std::shared_ptr<TextureHandle> faceSkin;     // multiclass only
    std::shared_ptr<TextureHandle> clothes;      // multiclass only
    std::shared_ptr<TextureHandle> others;       // multiclass only

    // True if the backend just produced a fresh inference this frame.
    // (Some backends throttle to every Nth frame for performance.)
    bool fresh = false;

    // Backend identification — useful for diagnostics
    std::string backendName;
};

// -----------------------------------------------------------------------------
// SegmenterBackend — the abstraction
//
// Lifecycle:
//   - Constructed via the factory in segmenter_backend_factory.h
//   - Initialized once with a neural backend (TFLite on Android, Core ML on
//     iOS) and a path to the model file
//   - run() called once per frame; returns the SegmentationChannels for
//     that frame
//   - Destroyed when the session ends (RAII cleans up GPU resources)
//
// Thread safety: run() must be called on the render thread.
// -----------------------------------------------------------------------------
class SegmenterBackend {
public:
    virtual ~SegmenterBackend() = default;

    // Human-readable name for diagnostics. e.g. "HairSegmenter",
    // "MulticlassSegmenter256". Does not change after construction.
    virtual std::string name() const = 0;

    // Approximate inference time on the active device, in milliseconds.
    // Returns 0 until at least one run() has completed (caller should treat
    // 0 as "unknown"). Used by Q2 quality-tier auto-selection.
    virtual float lastInferenceMs() const = 0;

    // Set of channels this backend actually produces. Channels not in this
    // set are guaranteed nullptr in the output.
    struct ChannelsProduced {
        bool background = false;
        bool hair = false;
        bool bodySkin = false;
        bool faceSkin = false;
        bool clothes = false;
        bool others = false;
    };
    virtual ChannelsProduced channelsProduced() const = 0;

    // Run inference on the current camera frame. The cameraTex argument is
    // typically the same OES/Metal texture that drove the rest of the
    // perception pipeline this frame.
    //
    // Returns the SegmentationChannels for the frame. The returned shared
    // pointers remain valid until the next call to run() — callers that
    // need to retain references across frames must do so explicitly.
    virtual SegmentationChannels run(const TextureHandle& cameraTex,
                                     RenderContext* ctx) = 0;
};

}  // namespace community_ar
