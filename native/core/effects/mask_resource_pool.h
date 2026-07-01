// mask_resource_pool.h
// =============================================================================
// Community AR — MaskResourcePool
//
// A typed pool of named mask textures, populated each frame by the
// PerceptionPipeline (segmentation masks) and the MaskRasterizer
// (landmark-derived masks). Effects look up the masks they declared as
// consumed; the graph guarantees they are populated by the time the
// effect's prepare() or render() runs.
//
// Naming convention (car-phase-3-requirements.md Decision 2):
//   masks.faceSkin         — multiclass segmenter face-skin channel
//   masks.bodySkin         — multiclass segmenter body-skin channel
//   masks.hair             — multiclass or dedicated hair segmenter output
//   masks.clothes          — multiclass clothes channel
//   masks.background       — multiclass background channel
//   masks.lipsContour      — landmark-rasterized lip mask (lazy, on demand)
//   masks.faceLandmarkSkin — landmark-rasterized skin polygon (fallback
//                            when the multiclass faceSkin channel is
//                            unavailable)
//
// Lifecycle:
//   - The pool is owned by EffectGraph (one pool per graph instance)
//   - Cleared at the start of each frame (textures stay allocated; just
//     the name → texture map is cleared)
//   - Populated by produceFromPerception() (segmenter masks) at frame start
//   - Effects with `produces` requirements populate additional masks
//     during their prepare() phase
//   - Effects with `consumes` requirements read via get() during prepare()
//     and render()
//
// Thread safety:
//   - All operations on the render thread only.
//   - The shared_ptrs held by the pool are not synchronized — don't pass
//     pool references to background threads.
// =============================================================================

#pragma once

#include "../perception/perception_frame.h"
#include "../render/render_context.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace community_ar {

class MaskResourcePool {
public:
    MaskResourcePool() = default;
    ~MaskResourcePool() = default;

    // Disable copy; pool ownership is exclusive to its owning EffectGraph.
    MaskResourcePool(const MaskResourcePool&) = delete;
    MaskResourcePool& operator=(const MaskResourcePool&) = delete;

    // Clear the name → texture map at the start of each frame. The
    // underlying textures are retained by their owners (segmenter backends,
    // rasterizers); the pool just lets go of its references.
    void clearForNewFrame();

    // Populate the standard mask names from a PerceptionFrame. Pulls the
    // segmenter output channels into the pool under their canonical names.
    // Called by EffectGraph once per frame after perception runs.
    void produceFromPerception(const PerceptionFrame& frame);

    // Register a mask under a given name. Used by effects with `produces`
    // requirements (e.g. a future iris-mask producer) or by the rasterizer
    // when emitting landmark-derived masks on demand.
    void put(const std::string& name, std::shared_ptr<TextureHandle> mask);

    // Look up a mask by name. Returns nullptr if no mask under that name
    // exists this frame. Effects that consume optional masks (e.g. one
    // that would prefer faceSkin but can fall back to faceLandmarkSkin)
    // check for nullptr and degrade gracefully.
    std::shared_ptr<TextureHandle> get(const std::string& name) const;

    // Returns true if a mask under the given name has been registered
    // this frame. Cheaper than get() when only existence matters.
    bool has(const std::string& name) const;

    // Diagnostic: list all currently registered mask names. Useful for
    // logging and for the example app's debug overlay.
    std::vector<std::string> listKeys() const;

    // Canonical mask names (string constants, also exported as kMask* for
    // typo-safety in calling code).
    static constexpr const char* kFaceSkin         = "masks.faceSkin";
    static constexpr const char* kBodySkin         = "masks.bodySkin";
    static constexpr const char* kHair             = "masks.hair";
    static constexpr const char* kClothes          = "masks.clothes";
    static constexpr const char* kBackground       = "masks.background";
    static constexpr const char* kLipsContour      = "masks.lipsContour";
    static constexpr const char* kFaceLandmarkSkin = "masks.faceLandmarkSkin";

private:
    std::unordered_map<std::string, std::shared_ptr<TextureHandle>> masks_;
};

}  // namespace community_ar
