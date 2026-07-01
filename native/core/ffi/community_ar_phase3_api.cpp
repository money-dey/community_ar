// community_ar_phase3_api.cpp
// =============================================================================
// Phase 3 C ABI implementation.
//
// Registers the SkinSmoothEffect factory with the Phase 2 effect registry,
// so adding a CAR_EFFECT_TYPE_SKIN_SMOOTH entry to a graph constructs an
// actual SkinSmoothEffect via the existing car_p2_graph_set() ABI.
//
// Implements the diagnostic functions declared in community_ar_phase3_api.h.
// =============================================================================

#include "community_ar_phase3_api.h"
#include "../effects/skin_smooth_effect.h"
#include "../effects/effect_types.h"
#include "../effects/effect_graph.h"
#include "../effects/mask_resource_pool.h"
#include "../phase0_session.h"
#include <memory>
#include <vector>
#include <string>

namespace community_ar {

// -----------------------------------------------------------------------------
// Beauty effect factory
//
// Called by the Phase 2 graph factory dispatcher (factoryForType in
// community_ar_phase2_api.cpp) when it sees CAR_EFFECT_TYPE_SKIN_SMOOTH.
// Validates the config struct version, builds a SkinSmoothEffect::Config,
// constructs the effect.
// -----------------------------------------------------------------------------
std::unique_ptr<Effect> makeSkinSmoothEffect(const void* configBytes,
                                              size_t configSize) {
    if (configSize < sizeof(CARBeautyFilterConfig)) return nullptr;
    const auto* cfg = static_cast<const CARBeautyFilterConfig*>(configBytes);
    if (cfg->version != 1) return nullptr;  // unknown version

    SkinSmoothEffect::Config sc;
    sc.smoothingStrength       = cfg->smoothingStrength;
    sc.detailPreserve          = cfg->detailPreserve;
    sc.blemishReduction        = cfg->blemishReduction;
    sc.bilateralEdgeSensitivity = cfg->bilateralEdgeSensitivity;
    sc.highFreqStrength        = cfg->highFreqStrength;
    sc.midFreqStrength         = cfg->midFreqStrength;
    sc.warmth                  = cfg->warmth;
    sc.highlightLift           = cfg->highlightLift;
    sc.clarity                 = cfg->clarity;
    sc.specularControl         = cfg->specularControl;
    sc.temporalSmoothing       = cfg->temporalSmoothing;
    sc.adaptivenessLocal       = cfg->adaptivenessLocal;

    // Map quality tier enum
    switch (cfg->qualityTier) {
        case CAR_BEAUTY_QUALITY_HIGH:   sc.quality = BeautyQuality::High; break;
        case CAR_BEAUTY_QUALITY_MEDIUM: sc.quality = BeautyQuality::Medium; break;
        case CAR_BEAUTY_QUALITY_LOW:    sc.quality = BeautyQuality::Low; break;
        case CAR_BEAUTY_QUALITY_AUTO:
        default:                        sc.quality = BeautyQuality::Auto; break;
    }

    return std::make_unique<SkinSmoothEffect>(std::move(sc));
}

}  // namespace community_ar

// -----------------------------------------------------------------------------
// Public C ABI implementations
// -----------------------------------------------------------------------------
using namespace community_ar;

extern "C" {

CAR_EXPORT uint32_t car_p3_beauty_effective_quality(CARSession* session) {
    if (!session) return CAR_BEAUTY_QUALITY_AUTO;
    auto* s = reinterpret_cast<Phase0Session*>(session);

    Effect* beautyEffect = s->effectGraph().findFirstEffectOfType(
        CAR_EFFECT_SKIN_SMOOTH);
    if (!beautyEffect) return CAR_BEAUTY_QUALITY_AUTO;

    auto* skinSmooth = static_cast<SkinSmoothEffect*>(beautyEffect);
    switch (skinSmooth->effectiveQuality()) {
        case BeautyQuality::High:   return CAR_BEAUTY_QUALITY_HIGH;
        case BeautyQuality::Medium: return CAR_BEAUTY_QUALITY_MEDIUM;
        case BeautyQuality::Low:    return CAR_BEAUTY_QUALITY_LOW;
        case BeautyQuality::Auto:
        default:                    return CAR_BEAUTY_QUALITY_AUTO;
    }
}

CAR_EXPORT uint32_t car_p3_mask_pool_list(CARSession* session,
                                           const char** outNames,
                                           uint32_t maxNames) {
    if (!session || !outNames) return 0;
    auto* s = reinterpret_cast<Phase0Session*>(session);

    auto& pool = s->effectGraph().maskPool();
    auto keys = pool.listKeys();

    // Static storage for the returned char*. listKeys() returns
    // std::string by value; we'd need a thread-safe cache here in
    // production. For Batch 3 scaffold, we use a thread_local static
    // vector that lives as long as the thread.
    //
    // Threading note: the perception/render thread is the only one that
    // should call this, so thread_local is sufficient.
    static thread_local std::vector<std::string> sCached;
    sCached = std::move(keys);

    uint32_t count = static_cast<uint32_t>(sCached.size());
    uint32_t toWrite = std::min(count, maxNames);
    for (uint32_t i = 0; i < toWrite; ++i) {
        outNames[i] = sCached[i].c_str();
    }
    return count;
}

}  // extern "C"
