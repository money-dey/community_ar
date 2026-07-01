# =============================================================================
# Community AR — Phase 1 consolidated native build
#
# Builds everything from Phase 0 (plumbing) and Phase 1 (perception), with
# both rounds of production fixes applied.
#
# Android: produces libcommunity_ar_native.so loaded by the Flutter plugin
# iOS:     produces a static library linked into the .framework
#
# External dependencies expected to be vendored or fetched separately:
#   - tensorflow-lite + GPU delegate (Android, via Maven)
#   - MediaPipe model files (.tflite on Android, compiled .mlmodelc on iOS),
#     placed under native/models/ at build time
# =============================================================================

cmake_minimum_required(VERSION 3.22)
project(community_ar_native CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -----------------------------------------------------------------------------
# Cross-platform sources (compiled on both Android and iOS)
# -----------------------------------------------------------------------------
set(CAR_SOURCES
    # Session + ABI
    native/core/phase0_session.cpp
    native/core/ffi/community_ar_phase0_api.cpp

    # Perception orchestrator
    native/core/perception/perception_pipeline.cpp

    # Perception components
    native/core/perception/one_euro_filter.cpp
    native/core/perception/face_tracker.cpp
    native/core/perception/face_landmarker.cpp
    native/core/perception/iris_landmarker.cpp
    native/core/perception/perception_models.cpp
    native/core/perception/skin_tone.cpp

    # Math
    native/core/math/pnp_solver.cpp

    # Render — debug overlay (cross-platform)
    native/core/render/debug_overlay.cpp
)

# -----------------------------------------------------------------------------
# Platform-specific sources
# -----------------------------------------------------------------------------
if(ANDROID)
    list(APPEND CAR_SOURCES
        # Render — GLES backend
        native/core/render/gles_render_context.cpp
        native/core/render/gles_compute.cpp

        # ML — TFLite
        native/core/ml/tflite_backend.cpp

        # JNI bridge
        android/src/main/cpp/jni_bridge.cpp
    )

    find_library(LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(GLES3_LIB GLESv3)
    find_library(EGL_LIB EGL)

    set(CAR_PLATFORM_LIBS ${LOG_LIB} ${ANDROID_LIB} ${GLES3_LIB} ${EGL_LIB})

    add_library(community_ar_native SHARED ${CAR_SOURCES})

elseif(APPLE)
    list(APPEND CAR_SOURCES
        # Render — Metal backend
        native/core/render/metal_render_context.mm
        native/core/render/metal_compute.mm

        # ML — Core ML
        native/core/ml/coreml_backend.mm
    )
    set(CAR_PLATFORM_LIBS
        "-framework Foundation"
        "-framework Metal"
        "-framework MetalKit"
        "-framework MetalPerformanceShaders"
        "-framework CoreML"
        "-framework CoreVideo"
        "-framework AVFoundation"
    )

    add_library(community_ar_native STATIC ${CAR_SOURCES})
endif()

# -----------------------------------------------------------------------------
# Include directories
# -----------------------------------------------------------------------------
target_include_directories(community_ar_native
    PUBLIC
        native/core/ffi
    PRIVATE
        native/core
)

target_link_libraries(community_ar_native PRIVATE ${CAR_PLATFORM_LIBS})

# -----------------------------------------------------------------------------
# External: TensorFlow Lite (Android only)
# -----------------------------------------------------------------------------
# Expected layout: third_party/tensorflow-lite/{include,lib}
# In production this is fetched via Gradle's externalNativeBuild + Maven, but
# during local development the headers can be vendored.
#
# if(ANDROID)
#     find_package(tensorflowlite REQUIRED)
#     target_link_libraries(community_ar_native PRIVATE
#         tensorflowlite::tensorflowlite
#         tensorflowlite::gpu_delegate)
# endif()

# -----------------------------------------------------------------------------
# Compile flags
# -----------------------------------------------------------------------------
target_compile_options(community_ar_native PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
    $<$<CONFIG:Release>:-O3 -ffast-math>
    $<$<CONFIG:Debug>:-O0 -g>
)

if(ANDROID AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(community_ar_native PROPERTIES
        LINK_FLAGS "-Wl,-s -Wl,--gc-sections")
endif()
