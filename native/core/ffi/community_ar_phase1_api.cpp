// community_ar_phase1_api.cpp
// =============================================================================
// Phase 1 C ABI implementation.
//
// This file did not exist until the AR bring-up (2026-07-04): the Phase 1
// header shipped with declarations only — another instance of the
// "documented but never implemented" consolidation gap, invisible until the
// first caller referenced a symbol and the link failed.
//
// Currently implemented for real: car_p1_get_perception_stats (feeds the
// example app's HUD — facesDetected is the key bring-up signal). The other
// three calls are accepted-but-inert until WP-E wires them through the
// session (they log once so on-device testing isn't misled).
// =============================================================================

#include "community_ar_phase1_api.h"
#include "../phase0_session.h"

#if defined(__ANDROID__)
#include <android/log.h>
#define P1_LOGW_ONCE(msg)                                              \
    do {                                                               \
        static bool _logged = false;                                   \
        if (!_logged) {                                                \
            _logged = true;                                            \
            __android_log_print(ANDROID_LOG_WARN, "CommunityAR", msg); \
        }                                                              \
    } while (0)
#else
#define P1_LOGW_ONCE(msg) ((void)0)
#endif

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
                                              uint32_t /*modeMask*/) {
    if (!session) return CAR_STATUS_INVALID_SESSION;
    P1_LOGW_ONCE("car_p1_set_debug_overlay: overlay rendering not wired yet (WP-E)");
    return CAR_STATUS_OK;
}

CAR_EXPORT CARStatus car_p1_set_one_euro_params(CARSession* session,
                                                float /*minCutoff*/,
                                                float /*beta*/,
                                                float /*dCutoff*/) {
    if (!session) return CAR_STATUS_INVALID_SESSION;
    P1_LOGW_ONCE("car_p1_set_one_euro_params: not wired yet (WP-E)");
    return CAR_STATUS_OK;
}

CAR_EXPORT CARStatus car_p1_force_perception(CARSession* session,
                                             const CARPerceptionRequest* req) {
    if (!session || !req) return CAR_STATUS_INVALID_SESSION;
    P1_LOGW_ONCE("car_p1_force_perception: not wired yet (WP-E)");
    return CAR_STATUS_OK;
}

}  // extern "C"
