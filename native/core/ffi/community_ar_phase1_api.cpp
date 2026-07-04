// community_ar_phase1_api.cpp
// =============================================================================
// Phase 1 C ABI implementation.
//
// This file did not exist until the AR bring-up (2026-07-04): the Phase 1
// header shipped with declarations only — another instance of the
// "documented but never implemented" consolidation gap, invisible until the
// first caller referenced a symbol and the link failed.
//
// All four calls are now real (WP-E): stats snapshot, debug-overlay mask,
// One-Euro tuning, forced perception. Overlay/forced state crosses to the
// render thread as latest-wins atomics on the session; filter params go
// through the render queue because they mutate per-track filter banks that
// only the render thread touches.
// =============================================================================

#include "community_ar_phase1_api.h"
#include "../phase0_session.h"
#include "../perception/perception_pipeline.h"  // complete type for the
                                                // queued setLandmarkFilterParams

using community_ar::Phase0Session;

extern "C" {

CAR_EXPORT void car_p1_get_perception_stats(CARSession* session,
                                            CARPerceptionStats* outStats) {
    if (!outStats) return;
    *outStats = CARPerceptionStats{};
    if (!session) return;
    auto* s = reinterpret_cast<Phase0Session*>(session);
    s->getPerceptionStatsSnapshot(outStats);
}

CAR_EXPORT CARStatus car_p1_set_debug_overlay(CARSession* session,
                                              uint32_t modeMask) {
    if (!session) return CAR_STATUS_INVALID_SESSION;
    auto* s = reinterpret_cast<Phase0Session*>(session);
    s->setDebugOverlayMask(modeMask);
    return CAR_STATUS_OK;
}

CAR_EXPORT CARStatus car_p1_set_one_euro_params(CARSession* session,
                                                float minCutoff,
                                                float beta,
                                                float dCutoff) {
    if (!session) return CAR_STATUS_INVALID_SESSION;
    auto* s = reinterpret_cast<Phase0Session*>(session);
    // Filter banks are per-track state owned by the render thread; mutate
    // them there. Values are safely captured by copy.
    s->runOnRenderThread([s, minCutoff, beta, dCutoff]() {
        s->perceptionPipeline().setLandmarkFilterParams(minCutoff, beta,
                                                        dCutoff);
    });
    return CAR_STATUS_OK;
}

CAR_EXPORT CARStatus car_p1_force_perception(CARSession* session,
                                             const CARPerceptionRequest* req) {
    if (!session || !req) return CAR_STATUS_INVALID_SESSION;
    auto* s = reinterpret_cast<Phase0Session*>(session);
    uint32_t bits = 0;
    if (req->needFaceLandmarks) bits |= community_ar::kForceFaceLandmarks;
    if (req->needIris)          bits |= community_ar::kForceIris;
    if (req->needHair)          bits |= community_ar::kForceHair;
    if (req->needSelfieSeg)     bits |= community_ar::kForceSelfieSeg;
    if (req->needPose)          bits |= community_ar::kForcePose;
    if (req->needSkinTone)      bits |= community_ar::kForceSkinTone;
    s->setForcedPerceptionBits(bits);
    return CAR_STATUS_OK;
}

}  // extern "C"
