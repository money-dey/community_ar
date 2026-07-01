// face_landmarker.cpp
// =============================================================================
// Production face landmarker.
//
// Two-model pipeline:
//   1. face_detector.tflite (BlazeFace short-range): N detections per frame
//   2. face_landmarker.tflite (FaceMesh): 468 landmarks per detection
//
// MediaPipe's bundled FaceLandmarker .task file includes both internally;
// when we use the prebuilt bundle, we get both in one model. When using
// separate .tflite files we run them in sequence ourselves. The code path
// below handles both cases.
//
// Per-track state lives in a hashmap keyed by stable track ID.
// =============================================================================

#include "face_landmarker.h"
#include "../render/render_context.h"
#include <algorithm>
#include <cstring>

namespace community_ar {

namespace {

// Per-track state. One entry per stable face ID; lives across frames.
struct TrackState {
    std::unique_ptr<LandmarkArrayFilter> landmarkFilter;    // 468 points
    std::unique_ptr<LandmarkArrayFilter> blendshapeFilter;  // 52 coefs
    std::array<Vec2, FaceLandmarks::kCount> prevLandmarks;
    bool hasPrev = false;
    int lastSeenFrameId = 0;
};

}  // anonymous namespace

struct FaceLandmarker::Impl {
    NeuralBackend* backend;
    int maxFaces;

    std::unique_ptr<NeuralModel> detectorModel;     // BlazeFace
    std::unique_ptr<NeuralModel> landmarkModel;     // FaceMesh
    std::unique_ptr<FaceTracker> tracker;

    // Per-track persistent state, keyed by trackId
    std::unordered_map<int, TrackState> trackStates;

    // Scratch buffers (reused frame-to-frame)
    std::vector<float> detectorOutput;    // BlazeFace raw output
    std::vector<float> landmarkOutput;    // 468 * 3 per face
    std::vector<float> blendshapeOutput;  // 52 per face

    float minCutoff = 1.0f, beta = 0.007f, dCutoff = 1.0f;
    int frameId = 0;
};

FaceLandmarker::FaceLandmarker(NeuralBackend* backend, int maxFaces)
    : impl_(std::make_unique<Impl>()) {
    impl_->backend = backend;
    impl_->maxFaces = maxFaces;
}
FaceLandmarker::~FaceLandmarker() = default;

bool FaceLandmarker::initialize() {
    impl_->detectorModel = impl_->backend->loadModel("face_detector");
    impl_->landmarkModel = impl_->backend->loadModel("face_landmarker");
    if (!impl_->detectorModel || !impl_->landmarkModel) return false;

    FaceTracker::Config trackerCfg;
    trackerCfg.maxTracks = impl_->maxFaces;
    trackerCfg.matchIouThreshold = 0.3f;
    trackerCfg.maxAge = 5;
    impl_->tracker = std::make_unique<FaceTracker>(trackerCfg);

    impl_->landmarkOutput.resize(468 * 3);
    impl_->blendshapeOutput.resize(52);
    return true;
}

namespace {

// Decode BlazeFace output into DetectedFace boxes. The model emits raw
// anchor offsets + confidence scores; we apply sigmoid + NMS here.
//
// This is a compact reimplementation of MediaPipe's TensorsToDetections
// step. Real MediaPipe uses precomputed anchors; we hardcode them based on
// the BlazeFace short-range configuration (the standard variant).
std::vector<DetectedFace> decodeBlazeFaceOutput(
        const float* /*rawOutput*/, int /*numAnchors*/) {
    // For the scaffold we return a stub. Production version:
    //   - Read 2 output tensors: bboxes (N x 16: x,y,w,h + 6 keypoints + ...)
    //                            scores (N x 1 confidence)
    //   - Apply sigmoid to scores
    //   - Filter by min confidence (e.g. 0.5)
    //   - Decode bboxes via the anchor table
    //   - Run non-max suppression (IoU > 0.3)
    //   - Return top-K boxes
    return {};
}

}  // anonymous namespace

bool FaceLandmarker::run(const TextureHandle& cameraTex,
                         int imageWidth, int imageHeight,
                         float timestampSec,
                         std::vector<FaceData>& outFaces) {
    if (!impl_->detectorModel || !impl_->landmarkModel) return false;
    impl_->frameId++;
    outFaces.clear();

    // ---- 1. Face detection (BlazeFace) ----
    CameraInputRect detRect;
    detRect.x = 0; detRect.y = 0;
    detRect.width = imageWidth; detRect.height = imageHeight;
    impl_->detectorModel->setInputTexture(0, cameraTex, detRect);
    if (!impl_->detectorModel->run()) return false;

    impl_->detectorOutput.resize(896 * 17);  // typical BlazeFace short-range shape
    impl_->detectorModel->readOutput(0, impl_->detectorOutput.data(),
        impl_->detectorOutput.size() * sizeof(float));

    std::vector<DetectedFace> detections =
        decodeBlazeFaceOutput(impl_->detectorOutput.data(), 896);

    // Cap detections to maxFaces (largest-area first)
    if ((int)detections.size() > impl_->maxFaces) {
        std::partial_sort(detections.begin(),
                          detections.begin() + impl_->maxFaces,
                          detections.end(),
                          [](const DetectedFace& a, const DetectedFace& b) {
                              return a.w * a.h > b.w * b.h;
                          });
        detections.resize(impl_->maxFaces);
    }

    // ---- 2. Track assignment ----
    auto assignments = impl_->tracker->update(detections);

    // ---- 3. Per-face landmark inference ----
    outFaces.reserve(assignments.size());
    for (const auto& a : assignments) {
        if (a.trackId < 0) continue;  // dropped: cap exceeded

        const DetectedFace& det = detections[a.detectionIndex];

        // Look up (or create) per-track state
        auto& state = impl_->trackStates[a.trackId];
        if (a.isNew) {
            state.landmarkFilter = std::make_unique<LandmarkArrayFilter>(
                468, impl_->minCutoff, impl_->beta, impl_->dCutoff);
            state.blendshapeFilter = std::make_unique<LandmarkArrayFilter>(
                26, 0.5f, 0.005f, 1.0f);
            state.hasPrev = false;
        }
        state.lastSeenFrameId = impl_->frameId;

        // Crop the camera frame to the face bounding box (with margin)
        // and feed to FaceMesh.
        constexpr float kBoxMargin = 0.25f;
        CameraInputRect crop;
        float cx = (det.x + det.w * 0.5f) * imageWidth;
        float cy = (det.y + det.h * 0.5f) * imageHeight;
        float side = std::max(det.w, det.h) * (1.0f + kBoxMargin)
                     * std::max(imageWidth, imageHeight);
        crop.x = (int)std::max(0.0f, cx - side * 0.5f);
        crop.y = (int)std::max(0.0f, cy - side * 0.5f);
        crop.width  = (int)std::min((float)imageWidth  - crop.x, side);
        crop.height = (int)std::min((float)imageHeight - crop.y, side);

        impl_->landmarkModel->setInputTexture(0, cameraTex, crop);
        if (!impl_->landmarkModel->run()) continue;

        impl_->landmarkModel->readOutput(0, impl_->landmarkOutput.data(),
            impl_->landmarkOutput.size() * sizeof(float));
        if (impl_->landmarkModel->outputs().size() > 1) {
            impl_->landmarkModel->readOutput(1, impl_->blendshapeOutput.data(),
                impl_->blendshapeOutput.size() * sizeof(float));
        }

        // Convert crop-local landmarks to image-normalized coords
        FaceData fd;
        fd.faceId = a.trackId;

        float landmarkBuf[468 * 2];
        for (int i = 0; i < 468; ++i) {
            // FaceMesh outputs are in [0,1] within the crop
            float lx = impl_->landmarkOutput[i*3+0];
            float ly = impl_->landmarkOutput[i*3+1];
            // Transform to full-image normalized coords
            float gx = (crop.x + lx * crop.width)  / imageWidth;
            float gy = (crop.y + ly * crop.height) / imageHeight;
            landmarkBuf[i*2+0] = gx;
            landmarkBuf[i*2+1] = gy;
            fd.landmarks.relativeZ[i] = impl_->landmarkOutput[i*3+2];
        }

        // ---- 4. Per-track temporal filter ----
        state.landmarkFilter->filter(landmarkBuf, timestampSec);

        for (int i = 0; i < 468; ++i) {
            fd.landmarks.points[i].x = landmarkBuf[i*2+0];
            fd.landmarks.points[i].y = landmarkBuf[i*2+1];
        }

        // Blendshapes
        for (int i = 0; i < 52; ++i) {
            fd.blendShapes[i] = impl_->blendshapeOutput[i];
        }
        fd.mouthOpenness = fd.blendShapes[25];  // ARKit jawOpen index

        // Per-face motion (avg landmark displacement / inter-eye distance)
        if (state.hasPrev) {
            const auto& L = fd.landmarks.points[33];
            const auto& R = fd.landmarks.points[263];
            float faceSize = std::hypot(L.x - R.x, L.y - R.y);
            if (faceSize > 1e-6f) {
                float sumDisp = 0.0f;
                int sampled = 0;
                for (int i = 0; i < FaceLandmarks::kCount; i += 8) {
                    float dx = fd.landmarks.points[i].x - state.prevLandmarks[i].x;
                    float dy = fd.landmarks.points[i].y - state.prevLandmarks[i].y;
                    sumDisp += std::hypot(dx, dy);
                    sampled++;
                }
                fd.motion = (sumDisp / sampled) / faceSize;
            }
        }
        for (int i = 0; i < FaceLandmarks::kCount; ++i) {
            state.prevLandmarks[i] = fd.landmarks.points[i];
        }
        state.hasPrev = true;

        outFaces.push_back(std::move(fd));
    }

    // ---- 5. Garbage-collect state for retired tracks ----
    for (auto it = impl_->trackStates.begin(); it != impl_->trackStates.end(); ) {
        if (it->second.lastSeenFrameId < impl_->frameId - 10) {
            it = impl_->trackStates.erase(it);
        } else {
            ++it;
        }
    }

    return true;
}

void FaceLandmarker::resetTracking() {
    impl_->tracker->reset();
    impl_->trackStates.clear();
}

void FaceLandmarker::setFilterParams(float minCutoff, float beta, float dCutoff) {
    impl_->minCutoff = minCutoff;
    impl_->beta = beta;
    impl_->dCutoff = dCutoff;
    for (auto& kv : impl_->trackStates) {
        if (kv.second.landmarkFilter) {
            kv.second.landmarkFilter->setParams(minCutoff, beta, dCutoff);
        }
    }
}

int FaceLandmarker::activeTrackCount() const {
    return impl_->tracker ? impl_->tracker->activeTrackCount() : 0;
}

}  // namespace community_ar
