// perception_pipeline_iris_skin_updates.cpp
// =============================================================================
// Targeted PerceptionPipeline::run() updates for the second-round Phase 1 fixes.
//
// Two changes:
//   1. IrisLandmarker.run() signature gained a timestampSec parameter for
//      One-Euro filtering; pass through from captureTimestampNs.
//   2. After processing all faces this frame, call retainOnly() on the iris
//      landmarker so its per-track filter state for retired faces is freed.
//      Mirror of the existing skin-tone GC.
//
// Skin tone integration is unchanged in shape — the SkinToneEstimator's
// implementation now actually dispatches compute, but its public API didn't
// change. The pipeline keeps the per-face map keyed by faceId.
// =============================================================================

#include "perception_pipeline.h"
#include "perception_pipeline_impl.h"
#include "face_landmarker.h"
#include "iris_landmarker.h"
#include "perception_models.h"
#include "skin_tone.h"
#include "../math/pnp_solver.h"
#include "../render/render_context.h"
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <unordered_map>

namespace community_ar {

// (forward decl from the multi-face fix file)
#if defined(__ANDROID__)
extern "C" void tflite_backend_invalidate_frame(NeuralBackend* b);
#endif
#if defined(__APPLE__)
extern "C" void coreml_backend_invalidate_frame(NeuralBackend* b);
#endif

// -----------------------------------------------------------------------------
// Construction / lifetime
//
// Builds the Phase 1 perception sub-models eagerly. The segmentation backend is
// created later via initSegmenterBackend(), once the session knows which model
// files exist on disk.
// -----------------------------------------------------------------------------
PerceptionPipeline::PerceptionPipeline(const PerceptionConfig& cfg,
                                       NeuralBackend* backend,
                                       RenderContext* ctx)
    : impl_(std::make_unique<Impl>()) {
    impl_->config  = cfg;
    impl_->backend = backend;
    impl_->ctx     = ctx;

    impl_->faceLandmarker = std::make_unique<FaceLandmarker>(backend, cfg.maxFaces);
    impl_->faceLandmarker->initialize();

    impl_->irisLandmarker = std::make_unique<IrisLandmarker>(backend);
    impl_->irisLandmarker->initialize();
}

PerceptionPipeline::~PerceptionPipeline() = default;

void PerceptionPipeline::setLandmarkFilterParams(float minCutoff, float beta,
                                                 float dCutoff) {
    if (impl_->faceLandmarker) {
        impl_->faceLandmarker->setFilterParams(minCutoff, beta, dCutoff);
    }
}

void PerceptionPipeline::unloadIdleModels() {
    // Placeholder: sub-models are currently retained for the session lifetime.
    // A future memory-pressure policy would release idle models here (they are
    // lazily reloaded on next use). No-op keeps the public contract stable.
}

const PerceptionFrame& PerceptionPipeline::run(
        const TextureHandle& cameraTexture,
        int64_t captureTimestampNs) {

    impl_->frameId++;
    auto& F = impl_->currentFrame;
    F.frameId = impl_->frameId;
    F.captureTimestampNs = captureTimestampNs;
    F.imageWidth  = cameraTexture.width();
    F.imageHeight = cameraTexture.height();
    F.faces.clear();
    F.hairMask = nullptr;
    F.teethMask = nullptr;
    F.beardMask = nullptr;

    // Invalidate the per-frame ML input blit cache
#if defined(__ANDROID__)
    tflite_backend_invalidate_frame(impl_->backend);
#elif defined(__APPLE__)
    coreml_backend_invalidate_frame(impl_->backend);
#endif

    float timestampSec = captureTimestampNs / 1e9f;

    // Face landmarks (multi-face)
    if (impl_->requirements.needsFaceLandmarks && impl_->faceLandmarker) {
        impl_->faceLandmarker->run(cameraTexture, F.imageWidth, F.imageHeight,
                                   timestampSec, F.faces);
    }

    // Per-face downstream perception
    for (auto& face : F.faces) {
        // Iris — now with both eyes + per-track temporal filtering
        if (impl_->requirements.needsIrisLandmarks && impl_->irisLandmarker) {
            impl_->irisLandmarker->run(cameraTexture,
                                       F.imageWidth, F.imageHeight,
                                       timestampSec, face);
        }

        // Face pose (PnP)
        if (impl_->requirements.needsFacePose) {
            float pts[FaceLandmarks::kCount * 2];
            for (int i = 0; i < FaceLandmarks::kCount; ++i) {
                pts[i*2+0] = face.landmarks.points[i].x * F.imageWidth;
                pts[i*2+1] = face.landmarks.points[i].y * F.imageHeight;
            }
            auto K = CameraIntrinsics::approximate(F.imageWidth, F.imageHeight);
            PnPResult pnp = solveFacePose(pts, FaceLandmarks::kCount, K);
            face.pose.valid = pnp.valid;
            face.pose.confidence = std::max(0.0f, 1.0f - pnp.reprojectionError / 8.0f);
            std::memcpy(face.pose.rotation, pnp.rotation, sizeof(face.pose.rotation));
            std::memcpy(face.pose.translation, pnp.translation, sizeof(face.pose.translation));
        }

        // Skin tone (per-face, async compute-dispatch backed)
        if (impl_->requirements.needsSkinTone) {
            auto& estPtr = impl_->skinToneByTrack[face.faceId];
            if (!estPtr) estPtr = std::make_unique<SkinToneEstimator>(impl_->ctx);
            estPtr->requestUpdate(cameraTexture, face.landmarks,
                                  (int)impl_->frameId);
            face.skinTone = estPtr->getCurrent();
        }
    }

    // GC: per-track state for faces that didn't appear this frame
    {
        std::vector<int> activeIds;
        activeIds.reserve(F.faces.size());
        std::unordered_set<int> activeSet;
        for (const auto& f : F.faces) {
            activeIds.push_back(f.faceId);
            activeSet.insert(f.faceId);
        }
        // Iris filters
        if (impl_->irisLandmarker) impl_->irisLandmarker->retainOnly(activeIds);
        // Skin tone estimators
        for (auto it = impl_->skinToneByTrack.begin();
             it != impl_->skinToneByTrack.end(); ) {
            if (activeSet.find(it->first) == activeSet.end()) {
                it = impl_->skinToneByTrack.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Whole-image segmentation via the Phase 3 segmenter backend. Populates
    // F.segmentationMasks and the compatibility F.hairMask when an effect
    // needs a mask channel this frame (gated inside runSegmenterForFrame by
    // segmenterNeededThisFrame).
    runSegmenterForFrame(cameraTexture, F, impl_->ctx);

    return F;
}

}  // namespace community_ar
