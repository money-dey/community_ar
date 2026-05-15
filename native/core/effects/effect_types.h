// effect_types.h
// =============================================================================
// Community AR — Effect type ID registry
//
// IDs are stability-bound forever once shipped. Never reuse a retired ID.
// New effects always get new IDs. Gap-based numbering reserves room for
// each bucket (A/B/C/D/E) to grow without ID collision.
//
// The single source of truth for which effect_type_id maps to which engine.
// Both Dart and C++ sides include this header (the Dart side gets it
// transcribed in lib/src/effects/effect_type_ids.dart).
// =============================================================================

#pragma once

#include <cstdint>

namespace community_ar {

// -----------------------------------------------------------------------------
// Bucket A — MaskedRecolor (IDs 1-31)
//
// All Bucket A effects use the MaskedRecolorEffect engine. They differ only
// in which landmark contour they consume and (sometimes) the blend math.
// -----------------------------------------------------------------------------
constexpr uint32_t CAR_EFFECT_LIPS              = 1;   // Phase 2
constexpr uint32_t CAR_EFFECT_IRIS              = 2;   // Phase 6
constexpr uint32_t CAR_EFFECT_TEETH             = 3;   // Phase 6
constexpr uint32_t CAR_EFFECT_BROWS             = 4;   // Phase 6
constexpr uint32_t CAR_EFFECT_UNDER_EYE         = 5;   // Phase 6
constexpr uint32_t CAR_EFFECT_HAIR_THICKEN      = 6;   // Phase 6
constexpr uint32_t CAR_EFFECT_BEARD_THICKEN     = 7;   // Phase 6
constexpr uint32_t CAR_EFFECT_SKIN_SMOOTH       = 8;   // Phase 3 (separate engine)
// IDs 9-31 reserved for future Bucket A variants

// -----------------------------------------------------------------------------
// Bucket B — LandmarkWarp (IDs 32-63)
//
// Geometric deformation effects. Uses the LandmarkWarp engine (Phase 4).
// -----------------------------------------------------------------------------
constexpr uint32_t CAR_EFFECT_EYE_ENLARGE       = 32;  // Phase 4
constexpr uint32_t CAR_EFFECT_NOSE_RESHAPE      = 33;  // Phase 4
constexpr uint32_t CAR_EFFECT_LIP_PLUMP         = 34;  // Phase 4
constexpr uint32_t CAR_EFFECT_FACE_SLIM         = 35;  // Phase 4
// IDs 36-63 reserved for future Bucket B variants

// -----------------------------------------------------------------------------
// Bucket C — AssetOverlay (IDs 64-127)
//
// 3D mesh and 2D sprite accessories rendered via Filament (Phase 5).
// -----------------------------------------------------------------------------
constexpr uint32_t CAR_EFFECT_GLASSES           = 64;  // Phase 5
constexpr uint32_t CAR_EFFECT_HAT               = 65;
constexpr uint32_t CAR_EFFECT_EARRING           = 66;
constexpr uint32_t CAR_EFFECT_NECKLACE          = 67;
constexpr uint32_t CAR_EFFECT_HAIRSTYLE_3D      = 68;  // Phase 7
constexpr uint32_t CAR_EFFECT_BEARD_3D          = 69;
constexpr uint32_t CAR_EFFECT_EYELASHES         = 70;
// IDs 71-127 reserved

// -----------------------------------------------------------------------------
// Bucket D — ImageGrade (IDs 128-159)
//
// LUT-based color grading + grain / vignette / light leaks (Phase 5.5).
// -----------------------------------------------------------------------------
constexpr uint32_t CAR_EFFECT_LUT_GRADE         = 128;
constexpr uint32_t CAR_EFFECT_FILM_GRAIN        = 129;
constexpr uint32_t CAR_EFFECT_VIGNETTE          = 130;

// -----------------------------------------------------------------------------
// Bucket E — Background (IDs 160-191)
//
// Selfie-segmentation-driven background effects (Phase 6).
// -----------------------------------------------------------------------------
constexpr uint32_t CAR_EFFECT_BACKGROUND_BLUR   = 160;
constexpr uint32_t CAR_EFFECT_BACKGROUND_REPLACE = 161;

}  // namespace community_ar
