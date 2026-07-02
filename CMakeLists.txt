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

    # Phase 2 — effects engine
    native/core/effects/effect_graph.cpp
    native/core/effects/mask_rasterizer.cpp
    native/core/effects/masked_recolor_effect.cpp
    native/core/effects/lips_effect.cpp
    native/core/ffi/community_ar_phase2_api.cpp
    native/core/phase0_session_phase2_updates.cpp

    # Phase 3 — perception backends (segmenter abstraction)
    native/core/perception/segmenter_backend_factory.cpp
    native/core/perception/hair_segmenter_backend.cpp
    native/core/perception/multiclass_segmenter_backend.cpp
    native/core/perception/perception_pipeline_phase3_updates.cpp

    # Phase 3 — effect graph upgrades + beauty
    native/core/effects/mask_resource_pool.cpp
    native/core/effects/skin_smooth_effect.cpp
    native/core/ffi/community_ar_phase3_api.cpp
)

# -----------------------------------------------------------------------------
# Platform-specific sources
# -----------------------------------------------------------------------------
if(ANDROID)
    list(APPEND CAR_SOURCES
        # Render — GLES backend
        native/core/render/gles_render_context.cpp
        native/core/render/gles_compute.cpp

        # JNI bridge
        android/src/main/cpp/jni_bridge.cpp
    )
    # ML — TFLite backend is added conditionally below (real impl if TFLite is
    # vendored, otherwise a stub) so the Phase 0 pipeline builds without it.

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
        native/core/render/metal_render_context_phase3_updates.mm

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
# native/core/ml/tflite_backend.cpp links the TFLite C API + GPU delegate (with
# GL-interop). Artifacts are vendored under third_party/tensorflow-lite/ in the
# layout below; populate them with `bash tools/fetch_tflite.sh`. Override the
# location with -DCAR_TFLITE_DIR=/path/to/tflite.
#
#   third_party/tensorflow-lite/
#   ├── include/                 (C API + gpu/gl headers)
#   └── lib/<ABI>/               (libtensorflowlite_c.so, libtensorflowlite_gpu_delegate.so)
#
# See third_party/tensorflow-lite/README.md — in particular the GL-interop
# caveat (the gpu/gl headers + TfLiteGpuDelegateBindGl* symbols require a
# from-source delegate build, not the standard prebuilt AAR).
# TFLite is OPTIONAL: if it's vendored, we compile the real backend and link it;
# otherwise we compile a stub factory (createTfliteBackend -> nullptr) so the
# library still builds and the Phase 0 camera pipeline runs. Perception is
# simply inactive until TFLite is vendored — vendor it with
# `bash tools/fetch_tflite.sh` (+ the from-source library build) to enable it.
if(ANDROID)
    set(CAR_TFLITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tensorflow-lite"
        CACHE PATH "Vendored TensorFlow Lite root (include/ + lib/<ABI>/)")

    set(_car_tflite_inc "${CAR_TFLITE_DIR}/include")
    set(_car_tflite_libdir "${CAR_TFLITE_DIR}/lib/${ANDROID_ABI}")

    find_library(CAR_TFLITE_C_LIB   tensorflowlite_c
                 PATHS "${_car_tflite_libdir}" NO_DEFAULT_PATH)
    find_library(CAR_TFLITE_GPU_LIB tensorflowlite_gpu_delegate
                 PATHS "${_car_tflite_libdir}" NO_DEFAULT_PATH)

    if(IS_DIRECTORY "${_car_tflite_inc}" AND CAR_TFLITE_C_LIB AND CAR_TFLITE_GPU_LIB)
        message(STATUS "Community AR: TensorFlow Lite found — building with perception (${ANDROID_ABI}).")
        target_sources(community_ar_native PRIVATE native/core/ml/tflite_backend.cpp)
        target_include_directories(community_ar_native PRIVATE "${_car_tflite_inc}")
        target_link_libraries(community_ar_native PRIVATE
            "${CAR_TFLITE_C_LIB}" "${CAR_TFLITE_GPU_LIB}")
    else()
        message(WARNING
            "Community AR: TensorFlow Lite NOT vendored for ABI '${ANDROID_ABI}' "
            "— building WITHOUT perception (Phase 0 camera pipeline only). "
            "Run `bash tools/fetch_tflite.sh` and build the libraries "
            "(see third_party/tensorflow-lite/README.md) to enable it.")
        target_sources(community_ar_native PRIVATE native/core/ml/tflite_backend_stub.cpp)
    endif()
endif()

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
