// mask_resource_pool.cpp
// =============================================================================
// Implementation of the named mask pool.
//
// The implementation is intentionally simple — an unordered_map of names
// to shared_ptr<TextureHandle>. The complexity is in the contract (who
// produces, who consumes, when), not the data structure.
// =============================================================================

#include "mask_resource_pool.h"
#include <algorithm>

namespace community_ar {

void MaskResourcePool::clearForNewFrame() {
    // Releases our reference to the textures. The shared_ptrs may keep
    // them alive elsewhere (e.g. the segmenter backend retains its own
    // long-lived references), which is intentional — we don't want to
    // free and reallocate every frame.
    masks_.clear();
}

void MaskResourcePool::produceFromPerception(const PerceptionFrame& frame) {
    // Populate from the segmenter output (Phase 3 channels). Each is
    // nullable if the active backend doesn't produce that channel.
    const auto& seg = frame.segmentationMasks;

    if (seg.faceSkin)   masks_[kFaceSkin]   = seg.faceSkin;
    if (seg.bodySkin)   masks_[kBodySkin]   = seg.bodySkin;
    if (seg.hair)       masks_[kHair]       = seg.hair;
    if (seg.clothes)    masks_[kClothes]    = seg.clothes;
    if (seg.background) masks_[kBackground] = seg.background;

    // Note: landmark-derived masks (kLipsContour, kFaceLandmarkSkin) are
    // NOT produced here. They're emitted lazily by the MaskRasterizer
    // when an effect's prepare() requests them, because:
    //   1. Producing them speculatively wastes GPU work if no effect needs them
    //   2. The rasterization depends on landmarks the effect's prepare()
    //      already has access to
    //   3. The rasterizer is part of the effect engine, not the perception
    //      layer
}

void MaskResourcePool::put(const std::string& name,
                            std::shared_ptr<TextureHandle> mask) {
    if (!mask) {
        // Putting nullptr is treated as a remove — simpler than a separate API.
        masks_.erase(name);
        return;
    }
    masks_[name] = std::move(mask);
}

std::shared_ptr<TextureHandle> MaskResourcePool::get(const std::string& name) const {
    auto it = masks_.find(name);
    return it != masks_.end() ? it->second : nullptr;
}

bool MaskResourcePool::has(const std::string& name) const {
    return masks_.find(name) != masks_.end();
}

std::vector<std::string> MaskResourcePool::listKeys() const {
    std::vector<std::string> keys;
    keys.reserve(masks_.size());
    for (const auto& [k, _] : masks_) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    return keys;
}

}  // namespace community_ar
