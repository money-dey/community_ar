// community_ar_phase0_api.cpp
// =============================================================================
// C ABI implementation. Translates extern "C" calls into C++ method calls
// on Phase0Session. Defensive: every public entry point checks its session
// pointer and catches exceptions so a bug here never crashes the host app.
// =============================================================================

#include "community_ar_phase0_api.h"
#include "../phase0_session.h"
#include <new>

using community_ar::Phase0Session;

namespace {
    Phase0Session* asSession(CARSession* s) {
        return reinterpret_cast<Phase0Session*>(s);
    }
}

extern "C" {

CAR_EXPORT CARSession* car_p0_create(const CARPhase0Config* cfg, CARStatus* outStatus) {
    if (!cfg) {
        if (outStatus) *outStatus = CAR_STATUS_INVALID_HANDLE;
        return nullptr;
    }
    try {
        auto* session = new Phase0Session(*cfg);
        if (outStatus) *outStatus = CAR_STATUS_OK;
        return reinterpret_cast<CARSession*>(session);
    } catch (const std::bad_alloc&) {
        if (outStatus) *outStatus = CAR_STATUS_INTERNAL_ERROR;
        return nullptr;
    } catch (...) {
        if (outStatus) *outStatus = CAR_STATUS_INTERNAL_ERROR;
        return nullptr;
    }
}

CAR_EXPORT void car_p0_destroy(CARSession* session) {
    delete asSession(session);
}

CAR_EXPORT CARStatus car_p0_submit_frame(CARSession* session,
                                         uint64_t cameraTextureHandle,
                                         int width,
                                         int height,
                                         int rotationDegrees,
                                         int isFrontFacing) {
    auto* s = asSession(session);
    if (!s) return CAR_STATUS_INVALID_HANDLE;
    try {
        return s->submitFrame(cameraTextureHandle, width, height,
                              rotationDegrees, isFrontFacing != 0);
    } catch (...) {
        return CAR_STATUS_INTERNAL_ERROR;
    }
}

CAR_EXPORT CARStatus car_p0_submit_frame_display(CARSession* session,
                                                 uint64_t cameraTextureHandle,
                                                 int width,
                                                 int height,
                                                 int rotationDegrees,
                                                 int isFrontFacing,
                                                 const float* texMatrix16) {
    auto* s = asSession(session);
    if (!s) return CAR_STATUS_INVALID_HANDLE;
    try {
        return s->submitFrameToDisplay(cameraTextureHandle, width, height,
                                       rotationDegrees, isFrontFacing != 0,
                                       texMatrix16);
    } catch (...) {
        return CAR_STATUS_INTERNAL_ERROR;
    }
}

CAR_EXPORT uint64_t car_p0_get_output_texture(CARSession* session) {
    auto* s = asSession(session);
    return s ? s->getOutputTexture() : 0;
}

CAR_EXPORT void car_p0_get_output_dimensions(CARSession* session,
                                             int* outWidth, int* outHeight) {
    auto* s = asSession(session);
    if (s) s->getOutputDimensions(outWidth, outHeight);
}

CAR_EXPORT CARStatus car_p0_set_test_mode(CARSession* session, CARTestMode mode) {
    auto* s = asSession(session);
    if (!s) return CAR_STATUS_INVALID_HANDLE;
    return s->setTestMode(mode);
}

CAR_EXPORT void car_p0_get_stats(CARSession* session, CARPhase0Stats* outStats) {
    auto* s = asSession(session);
    if (s) s->getStats(outStats);
}

CAR_EXPORT const char* car_p0_version_string(void) {
    return "Community AR Phase 0 — 0.0.1";
}

}  // extern "C"
