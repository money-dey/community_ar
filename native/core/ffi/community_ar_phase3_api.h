// community_ar_phase3_api.h
// =============================================================================
// Community AR — Phase 3 C ABI additions
//
// Adds:
//   - CARBeautyFilterConfig struct (versioned, 60 bytes)
//   - CARBeautyQuality enum
//   - Diagnostic accessors for effective quality tier and mask pool state
//
// The existing car_p2_graph_set / car_p2_graph_clear functions handle
// adding/removing the beauty effect — no new graph mutation functions
// needed. To install beauty, callers push a graph that includes a
// CAR_EFFECT_TYPE_SKIN_SMOOTH entry alongside whatever other effects
// they want.
//
// Extends the Phase 2 ABI without modifying it. All Phase 0/1/2 functions
// continue working unchanged.
// =============================================================================

#ifndef COMMUNITY_AR_PHASE3_API_H
#define COMMUNITY_AR_PHASE3_API_H

#include "community_ar_phase2_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Beauty quality tier
// -----------------------------------------------------------------------------
#define CAR_BEAUTY_QUALITY_AUTO     0
#define CAR_BEAUTY_QUALITY_HIGH     1
#define CAR_BEAUTY_QUALITY_MEDIUM   2
#define CAR_BEAUTY_QUALITY_LOW      3

// -----------------------------------------------------------------------------
// CARBeautyFilterConfig — version 1
//
// All fields little-endian on the Dart side, host byte order on native.
// Total: 60 bytes (4 + 13*4 + 4).
//
// Field documentation matches lib/src/effects/beauty_filter_config.dart;
// see that file for value ranges and visual interpretations.
//
// Layout for the Dart serializer:
//   offset  field                          size
//      0    version (= 1)                  uint32, 4
//      4    smoothingStrength              float32, 4
//      8    detailPreserve                 float32, 4
//     12    blemishReduction               float32, 4
//     16    bilateralEdgeSensitivity       float32, 4
//     20    highFreqStrength               float32, 4
//     24    midFreqStrength                float32, 4
//     28    warmth                         float32, 4
//     32    highlightLift                  float32, 4
//     36    clarity                        float32, 4
//     40    specularControl                float32, 4
//     44    temporalSmoothing              float32, 4
//     48    adaptivenessLocal              float32, 4
//     52    qualityTier                    uint32, 4
//     56    (padding, reserved)            4
// -----------------------------------------------------------------------------
typedef struct {
    uint32_t version;                  /* MUST be 1 for this struct layout */
    float    smoothingStrength;        /* [0, 1], 0=off, 1=max */
    float    detailPreserve;           /* [0.5, 1], detail vs smooth balance */
    float    blemishReduction;         /* [0, 1] */
    float    bilateralEdgeSensitivity; /* [0, 1] */
    float    highFreqStrength;         /* [0, 1], pore-level detail */
    float    midFreqStrength;          /* [0, 1], wrinkle attenuation */
    float    warmth;                   /* [-0.2, 0.2] */
    float    highlightLift;            /* [0, 0.3] */
    float    clarity;                  /* [0, 0.5] */
    float    specularControl;          /* [-1, 1], matte..glow */
    float    temporalSmoothing;        /* [0, 1] */
    float    adaptivenessLocal;        /* [0, 1] */
    uint32_t qualityTier;              /* CAR_BEAUTY_QUALITY_* */
    uint32_t reserved;                 /* keep struct size 8-aligned */
} CARBeautyFilterConfig;

// -----------------------------------------------------------------------------
// Diagnostics
//
// After a beauty effect has been installed, callers can query the effective
// quality tier (relevant when the user requested Auto). Returns:
//   CAR_BEAUTY_QUALITY_HIGH/MEDIUM/LOW after resolution, or
//   CAR_BEAUTY_QUALITY_AUTO if no beauty effect is installed or resolution
//   hasn't run yet.
// -----------------------------------------------------------------------------
CAR_EXPORT uint32_t car_p3_beauty_effective_quality(CARSession* session);

// -----------------------------------------------------------------------------
// List mask names currently in the pool (for the example app's debug
// overlay). Writes up to maxNames C-strings into outNames; returns the
// actual number of names available (which may exceed maxNames). The
// strings point into static storage and remain valid until the session
// is destroyed.
// -----------------------------------------------------------------------------
CAR_EXPORT uint32_t car_p3_mask_pool_list(CARSession* session,
                                          const char** outNames,
                                          uint32_t maxNames);

#ifdef __cplusplus
}
#endif

#endif
