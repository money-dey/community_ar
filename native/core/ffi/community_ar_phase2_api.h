// community_ar_phase2_api.h
// =============================================================================
// Community AR — Phase 2 C ABI additions
//
// Adds:
//   - Effect type IDs and config struct definitions
//   - car_p2_graph_set() / car_p2_graph_clear() for atomic graph replacement
//
// Extends the Phase 0 + Phase 1 ABIs without modifying them.
// =============================================================================

#ifndef COMMUNITY_AR_PHASE2_API_H
#define COMMUNITY_AR_PHASE2_API_H

#include "community_ar_phase1_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Effect type IDs — mirrored from effect_types.h for C consumers.
// Stability-bound forever; never reuse retired IDs.
// -----------------------------------------------------------------------------
#define CAR_EFFECT_TYPE_LIPS               1
#define CAR_EFFECT_TYPE_IRIS               2   /* Phase 6 */
#define CAR_EFFECT_TYPE_TEETH              3   /* Phase 6 */
#define CAR_EFFECT_TYPE_BROWS              4   /* Phase 6 */
#define CAR_EFFECT_TYPE_UNDER_EYE          5   /* Phase 6 */
#define CAR_EFFECT_TYPE_HAIR_THICKEN       6   /* Phase 6 */
#define CAR_EFFECT_TYPE_BEARD_THICKEN      7   /* Phase 6 */
#define CAR_EFFECT_TYPE_SKIN_SMOOTH        8   /* Phase 3 */

// -----------------------------------------------------------------------------
// LipsEffect config — version 1
//
// All fields are little-endian on the Dart side and assumed to match the
// host byte order on the native side. The version field MUST be the first
// field; new fields are appended to future versions only.
// -----------------------------------------------------------------------------
typedef struct {
    uint32_t version;            /* MUST be 1 for this struct layout */
    float    colorR;             /* sRGB, [0, 1] */
    float    colorG;
    float    colorB;
    float    opacity;            /* [0, 1], overall effect strength */
    float    edgeSoftness;       /* [0, 1], 0 = sharp, 1 = very soft */
    float    luminancePreserve;  /* [0, 1], 1 = keep source brightness */
} CARLipsEffectConfig;

/*  Layout reminder for the Dart serializer (28 bytes total):
    offset  field
       0    version              (uint32, 4 bytes)
       4    colorR               (float32, 4 bytes)
       8    colorG               (float32, 4 bytes)
      12    colorB               (float32, 4 bytes)
      16    opacity              (float32, 4 bytes)
      20    edgeSoftness         (float32, 4 bytes)
      24    luminancePreserve    (float32, 4 bytes)
*/

// -----------------------------------------------------------------------------
// Graph management
//
// Atomic replacement model: every change to the graph (including parameter
// updates) is a full graph rebuild. The Dart side packages the new graph,
// hands it to this call, and the Phase 0 session installs it before the
// next frame renders.
//
// Multiple effects can be installed in one call. effect_count is the size
// of each parallel array.
//
//   effect_type_ids:  [count]    — one type ID per effect
//   configs:          [count]    — one pointer to config bytes per effect
//   config_sizes:     [count]    — one size in bytes per effect's config
//
// The native side COPIES the config bytes; the caller can free them
// immediately after this call returns.
// -----------------------------------------------------------------------------
CAR_EXPORT CARStatus car_p2_graph_set(
    CARSession*           session,
    uint32_t              effect_count,
    const uint32_t*       effect_type_ids,
    const void* const*    configs,
    const size_t*         config_sizes);

CAR_EXPORT CARStatus car_p2_graph_clear(CARSession* session);

// -----------------------------------------------------------------------------
// Frame submission (AR display path) — added for the AR platform integration
// (docs/AR_INTEGRATION_SPEC.md, WP-B). Subsumes car_p0_submit_frame_display:
//
//   1. Ingress-copies the camera OES texture into an upright 2D texture,
//      applying `texMatrix16` (column-major 4x4, null = identity) once.
//   2. If an effect graph is installed: runs perception + the effect graph
//      offscreen and blits the result to the default framebuffer.
//      If not: presents the ingress texture through the Phase 0 test-mode
//      shader — pixel-identical to the Phase 0 display path.
//
// The caller owns the EGL window surface and presents with eglSwapBuffers
// afterwards. `width`/`height` are the display (window-surface) dimensions.
// `timestampNs` is the camera frame's capture timestamp (SurfaceTexture
// timestamp on Android), consumed by perception and the One-Euro filters.
// Must be called on the render thread whose current EGL context owns fbo 0.
//
// NEW symbol; all Phase 0/1/2 symbols are unchanged (C ABI stability
// invariant — CLAUDE.md §2).
// -----------------------------------------------------------------------------
CAR_EXPORT CARStatus car_p2_submit_frame_display(CARSession* session,
                                                 uint64_t cameraTextureHandle,
                                                 int width,
                                                 int height,
                                                 const float* texMatrix16,
                                                 int64_t timestampNs);

// -----------------------------------------------------------------------------
// Diagnostics — count of currently installed effects
// -----------------------------------------------------------------------------
CAR_EXPORT uint32_t car_p2_graph_effect_count(CARSession* session);

#ifdef __cplusplus
}
#endif

#endif
