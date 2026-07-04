// perception_log.h
// =============================================================================
// Shared logging macros for perception modules.
//
// Perception runs every frame, so a persistent failure must log ONCE, not
// spam logcat at 30 fps — CAR_PERC_LOGE_ONCE gives each call site its own
// static flag. Extracted from face_landmarker.cpp when iris_landmarker.cpp
// became the second user (per the CLAUDE.md "refactor at the second use
// site" convention).
// =============================================================================

#pragma once

#if defined(__ANDROID__)
#include <android/log.h>
#define CAR_PERC_LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, "CommunityAR-Perc", __VA_ARGS__)
#define CAR_PERC_LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, "CommunityAR-Perc", __VA_ARGS__)
#else
#define CAR_PERC_LOGE(...) ((void)0)
#define CAR_PERC_LOGI(...) ((void)0)
#endif

#define CAR_PERC_LOGE_ONCE(...)                                       \
    do {                                                              \
        static bool _logged = false;                                  \
        if (!_logged) { _logged = true; CAR_PERC_LOGE(__VA_ARGS__); } \
    } while (0)
