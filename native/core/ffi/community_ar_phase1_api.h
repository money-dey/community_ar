// community_ar_phase1_api.h
// =============================================================================
// Community AR — Phase 1 C ABI additions
//
// Extends the Phase 0 ABI with:
//   - Debug overlay mode selection
//   - Perception statistics readback
//   - Filter parameter tuning (One-Euro params)
//
// Maintains compatibility with Phase 0: all Phase 0 symbols remain.
// =============================================================================

#ifndef COMMUNITY_AR_PHASE1_API_H
#define COMMUNITY_AR_PHASE1_API_H

#include "community_ar_phase0_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Debug overlay modes (bit flags — combine with OR)
// -----------------------------------------------------------------------------
typedef enum {
    CAR_DEBUG_NONE       = 0,
    CAR_DEBUG_LANDMARKS  = 1 << 0,
    CAR_DEBUG_MESH       = 1 << 1,
    CAR_DEBUG_IRIS       = 1 << 2,
    CAR_DEBUG_HAIR_MASK  = 1 << 3,
    CAR_DEBUG_POSE       = 1 << 4,
    CAR_DEBUG_SKIN_TONE  = 1 << 5,
    CAR_DEBUG_ALL        = 0xFFFFFFFF
} CARDebugOverlay;

CAR_EXPORT CARStatus car_p1_set_debug_overlay(CARSession* session,
                                              uint32_t modeMask);

// -----------------------------------------------------------------------------
// Filter parameter tuning
// -----------------------------------------------------------------------------
CAR_EXPORT CARStatus car_p1_set_one_euro_params(CARSession* session,
                                                float minCutoff,
                                                float beta,
                                                float dCutoff);

// -----------------------------------------------------------------------------
// Perception statistics (in addition to Phase 0 frame stats)
// -----------------------------------------------------------------------------
typedef struct {
    int    facesDetected;
    float  faceMeshInferenceMs;
    float  irisInferenceMs;
    float  hairSegInferenceMs;
    float  pnpSolveMs;
    int    activeFilterCount;
    float  skinBaselineLuma;
    int    skinToneValid;       // 0 or 1
} CARPerceptionStats;

CAR_EXPORT void car_p1_get_perception_stats(CARSession* session,
                                            CARPerceptionStats* outStats);

// -----------------------------------------------------------------------------
// Force-enable specific perception modules without an active effect.
// Useful for the debug overlay to demand modules that no effect requested.
// -----------------------------------------------------------------------------
typedef struct {
    int needFaceLandmarks;
    int needIris;
    int needHair;
    int needSelfieSeg;
    int needPose;
    int needSkinTone;
} CARPerceptionRequest;

CAR_EXPORT CARStatus car_p1_force_perception(CARSession* session,
                                             const CARPerceptionRequest* req);

#ifdef __cplusplus
}
#endif

#endif
