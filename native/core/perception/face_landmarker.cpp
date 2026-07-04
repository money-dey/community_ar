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
#include <cmath>
#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#define CAR_PERC_LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, "CommunityAR-Perc", __VA_ARGS__)
#else
#define CAR_PERC_LOGE(...) ((void)0)
#endif
// Perception runs every frame; a persistent failure must log once, not spam
// (each call site gets its own static flag).
#define CAR_PERC_LOGE_ONCE(...)                                     \
    do {                                                            \
        static bool _logged = false;                                \
        if (!_logged) { _logged = true; CAR_PERC_LOGE(__VA_ARGS__); } \
    } while (0)

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
    std::vector<float> detectorOutput;    // BlazeFace regressors [N, 16]
    std::vector<float> detectorScores;    // BlazeFace scores [N, 1]
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

// Decode BlazeFace output into DetectedFace boxes — the real implementation
// (the long-stubbed version here returned {} unconditionally, which is why
// no face was ever detected on-device; see the CLAUDE.md gotcha).
//
// Compact reimplementation of MediaPipe's SsdAnchorsCalculator +
// TensorsToDetections for the BlazeFace short-range configuration:
//   strides {8, 16, 16, 16}, fixed_anchor_size, interpolated aspect 1.0
//   → 2 anchors/cell on the stride-8 grid, the three stride-16 layers share
//     one grid at 6/cell. For a 128×128 input: 16·16·2 + 8·8·6 = 896 anchors
//   (if the model reports a different count, the variant doesn't match this
//   config — see the CLAUDE.md "896 anchors" gotcha).
// The regressor tensor is [N,16]: box (dx, dy, w, h in input-pixel units,
// anchor-relative) + 6 keypoints (unused here). x/y/w/h scale = input size.
void buildShortRangeAnchors(int inputSize,
                            std::vector<std::pair<float, float>>& out) {
    out.clear();
    const int strides[4] = {8, 16, 16, 16};
    int i = 0;
    while (i < 4) {
        const int stride = strides[i];
        int same = 0;
        while (i + same < 4 && strides[i + same] == stride) same++;
        const int grid = inputSize / stride;
        const int anchorsPerCell = 2 * same;
        for (int y = 0; y < grid; ++y) {
            for (int x = 0; x < grid; ++x) {
                for (int a = 0; a < anchorsPerCell; ++a) {
                    out.emplace_back((x + 0.5f) / grid, (y + 0.5f) / grid);
                }
            }
        }
        i += same;
    }
}

float boxIoU(const DetectedFace& a, const DetectedFace& b) {
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.w, b.x + b.w);
    const float y2 = std::min(a.y + a.h, b.y + b.h);
    const float inter = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
    const float uni = a.w * a.h + b.w * b.h - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

std::vector<DetectedFace> decodeBlazeFaceOutput(const float* regressors,
                                                const float* scores,
                                                int numAnchors,
                                                int inputSize) {
    static std::vector<std::pair<float, float>> anchors;  // render thread only
    static int anchorsBuiltFor = 0;
    if (anchorsBuiltFor != inputSize) {
        buildShortRangeAnchors(inputSize, anchors);
        anchorsBuiltFor = inputSize;
    }
    if ((int)anchors.size() != numAnchors) {
        CAR_PERC_LOGE_ONCE(
            "BlazeFace anchor mismatch: model has %d, config yields %zu "
            "(input %d) — wrong model variant?",
            numAnchors, anchors.size(), inputSize);
        return {};
    }

    constexpr float kMinScore = 0.5f;   // post-sigmoid
    constexpr float kNmsIoU   = 0.3f;
    const float invScale = 1.0f / (float)inputSize;

    std::vector<DetectedFace> cands;
    for (int i = 0; i < numAnchors; ++i) {
        float logit = scores[i];
        logit = std::max(-80.0f, std::min(80.0f, logit));
        const float score = 1.0f / (1.0f + std::exp(-logit));
        if (score < kMinScore) continue;

        const float* r = regressors + (size_t)i * 16;
        const float cx = r[0] * invScale + anchors[i].first;
        const float cy = r[1] * invScale + anchors[i].second;
        const float w  = r[2] * invScale;
        const float h  = r[3] * invScale;
        if (w <= 0.0f || h <= 0.0f) continue;

        DetectedFace d;
        d.x = cx - w * 0.5f;
        d.y = cy - h * 0.5f;
        d.w = w;
        d.h = h;
        d.confidence = score;
        cands.push_back(d);
    }

    // Greedy NMS, highest confidence first.
    std::sort(cands.begin(), cands.end(),
              [](const DetectedFace& a, const DetectedFace& b) {
                  return a.confidence > b.confidence;
              });
    std::vector<DetectedFace> kept;
    for (const auto& c : cands) {
        bool overlaps = false;
        for (const auto& k : kept) {
            if (boxIoU(c, k) > kNmsIoU) { overlaps = true; break; }
        }
        if (!overlaps) {
            kept.push_back(c);
            if (kept.size() >= 10) break;
        }
    }
    return kept;
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
    // MediaPipe's face_detection preprocessing normalizes to [-1, 1]
    // (FaceMesh and Iris take [0, 1]); without this the detector sees a
    // shifted input distribution and scores collapse below threshold.
    detRect.signedInput = true;
    impl_->detectorModel->setInputTexture(0, cameraTex, detRect);
    if (!impl_->detectorModel->run()) return false;

    // The model emits TWO tensors — regressors [1, N, 16] and scores
    // [1, N, 1] — whose order varies by converter version, so identify them
    // by shape. (The old code read a single hardcoded 896×17 buffer; the
    // size check inside readOutput failed silently and the decode always ran
    // on zeros.)
    const auto& detOuts = impl_->detectorModel->outputs();
    int regIdx = -1, scoreIdx = -1, numAnchors = 0;
    for (size_t i = 0; i < detOuts.size(); ++i) {
        const auto& sh = detOuts[i].shape;
        const int channels = sh.rank >= 3 ? sh.dims[2] : 0;
        if (channels == 16)     { regIdx = (int)i; numAnchors = sh.dims[1]; }
        else if (channels == 1) { scoreIdx = (int)i; }
    }
    if (regIdx < 0 || scoreIdx < 0 || numAnchors <= 0) {
        CAR_PERC_LOGE_ONCE(
            "face_detector output layout unrecognized (%zu outputs) — "
            "expected [1,N,16] + [1,N,1]", detOuts.size());
        return false;
    }

    impl_->detectorOutput.resize((size_t)numAnchors * 16);
    impl_->detectorScores.resize((size_t)numAnchors);
    if (!impl_->detectorModel->readOutput(regIdx, impl_->detectorOutput.data(),
            impl_->detectorOutput.size() * sizeof(float)) ||
        !impl_->detectorModel->readOutput(scoreIdx, impl_->detectorScores.data(),
            impl_->detectorScores.size() * sizeof(float))) {
        CAR_PERC_LOGE_ONCE("face_detector readOutput failed (N=%d)", numAnchors);
        return false;
    }

    // Detector input size (square) from the input tensor [1, H, W, C].
    const auto& detIns = impl_->detectorModel->inputs();
    const int detInputSize =
        (!detIns.empty() && detIns[0].shape.rank >= 3 && detIns[0].shape.dims[1] > 0)
            ? detIns[0].shape.dims[1] : 128;

    std::vector<DetectedFace> detections = decodeBlazeFaceOutput(
        impl_->detectorOutput.data(), impl_->detectorScores.data(),
        numAnchors, detInputSize);

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
