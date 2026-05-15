// iris_landmarker.cpp
// =============================================================================
// Production implementation.
//
// MediaPipe's Iris model is trained on the LEFT eye in the model's frame of
// reference. To run it on the right eye, we feed a horizontally mirrored
// crop of the right eye region, then mirror the output X coordinates back.
// This means we ship and run ONE model, not two — a 1MB+ savings on app size.
//
// Per-track state lives in a hashmap keyed by faceId. When a face retires
// (signaled by the perception pipeline via retainOnly()), its iris filters
// are freed.
// =============================================================================

#include "iris_landmarker.h"
#include "../render/render_context.h"
#include <algorithm>
#include <unordered_set>

namespace community_ar {

namespace {

// Canonical MediaPipe FaceMesh indices for eye corners.
// (outer = lateral corner; inner = corner near the nose)
constexpr int kLeftEyeOuter  = 33;
constexpr int kLeftEyeInner  = 133;
constexpr int kRightEyeOuter = 263;
constexpr int kRightEyeInner = 362;

// Per-face per-eye temporal filter state.
struct IrisTrackState {
    // 5 contour points + 1 derived center per eye
    std::unique_ptr<LandmarkArrayFilter> leftFilter;   // 5 points
    std::unique_ptr<LandmarkArrayFilter> rightFilter;
    OneEuroFilter2D leftCenter;
    OneEuroFilter2D rightCenter;
    OneEuroFilter1D leftRadius;
    OneEuroFilter1D rightRadius;
};

// Computes the square crop bounding the eye region.
// Returns center + half-size in normalized image coords.
struct EyeBounds {
    float cx, cy;       // center, normalized image coords
    float halfSize;     // crop half-width, normalized
};

EyeBounds computeEyeBounds(const FaceLandmarks& landmarks,
                           int outerIdx, int innerIdx) {
    const auto& outer = landmarks.points[outerIdx];
    const auto& inner = landmarks.points[innerIdx];
    EyeBounds b;
    b.cx = (outer.x + inner.x) * 0.5f;
    b.cy = (outer.y + inner.y) * 0.5f;
    float eyeWidth = std::abs(inner.x - outer.x);
    // 1.6× eye width as crop side, clamped to a sensible minimum for small faces
    b.halfSize = std::max(eyeWidth * 0.8f, 0.04f);
    return b;
}

}  // anonymous namespace

struct IrisLandmarker::Impl {
    NeuralBackend* backend;
    std::unique_ptr<NeuralModel> model;
    std::vector<float> outputBuf;       // raw model output

    // Per-track filter banks, keyed by faceId
    std::unordered_map<int, IrisTrackState> trackStates;

    float minCutoff = 1.5f;   // slightly stiffer than face landmarks
    float beta = 0.01f;        // iris jitter is small; allow stronger smoothing
    float dCutoff = 1.0f;
};

IrisLandmarker::IrisLandmarker(NeuralBackend* backend)
    : impl_(std::make_unique<Impl>()) {
    impl_->backend = backend;
}
IrisLandmarker::~IrisLandmarker() = default;

bool IrisLandmarker::initialize() {
    impl_->model = impl_->backend->loadModel("iris_landmark");
    if (!impl_->model) return false;
    // MediaPipe Iris: output is per-eye, 5 landmarks (10 floats x,y) plus 5
    // for the contour normals — exact size varies by model version, so we
    // allocate generously and only read what we need.
    impl_->outputBuf.resize(32);
    return true;
}

bool IrisLandmarker::run(const TextureHandle& cameraTex,
                         int imageWidth, int imageHeight,
                         float timestampSec,
                         FaceData& face) {
    if (!impl_->model) {
        face.iris.valid = false;
        return false;
    }

    // Look up or create per-track filter state
    auto& state = impl_->trackStates[face.faceId];
    if (!state.leftFilter) {
        state.leftFilter = std::make_unique<LandmarkArrayFilter>(
            5, impl_->minCutoff, impl_->beta, impl_->dCutoff);
        state.rightFilter = std::make_unique<LandmarkArrayFilter>(
            5, impl_->minCutoff, impl_->beta, impl_->dCutoff);
        state.leftCenter   = OneEuroFilter2D(impl_->minCutoff, impl_->beta, impl_->dCutoff);
        state.rightCenter  = OneEuroFilter2D(impl_->minCutoff, impl_->beta, impl_->dCutoff);
        state.leftRadius   = OneEuroFilter1D(impl_->minCutoff, impl_->beta * 0.5f, impl_->dCutoff);
        state.rightRadius  = OneEuroFilter1D(impl_->minCutoff, impl_->beta * 0.5f, impl_->dCutoff);
    }

    bool anySuccess = false;

    // ---- LEFT EYE pass ----
    {
        EyeBounds b = computeEyeBounds(face.landmarks, kLeftEyeOuter, kLeftEyeInner);

        CameraInputRect crop;
        crop.x = (int)std::max(0.0f, (b.cx - b.halfSize) * imageWidth);
        crop.y = (int)std::max(0.0f, (b.cy - b.halfSize) * imageHeight);
        crop.width  = (int)std::min((float)imageWidth  - crop.x, b.halfSize * 2 * imageWidth);
        crop.height = (int)std::min((float)imageHeight - crop.y, b.halfSize * 2 * imageHeight);
        crop.rotationDeg = 0;
        crop.mirrorX = false;   // left eye: model native orientation

        impl_->model->setInputTexture(0, cameraTex, crop);
        if (impl_->model->run()) {
            impl_->model->readOutput(0, impl_->outputBuf.data(),
                                     impl_->outputBuf.size() * sizeof(float));

            // Convert crop-local [0,1] coords to image-normalized coords
            float contourBuf[10];  // 5 points × (x, y)
            float cx = 0, cy = 0;
            for (int i = 0; i < 5; ++i) {
                float lx = impl_->outputBuf[i*2 + 0];
                float ly = impl_->outputBuf[i*2 + 1];
                float gx = (b.cx - b.halfSize) + lx * (b.halfSize * 2);
                float gy = (b.cy - b.halfSize) + ly * (b.halfSize * 2);
                contourBuf[i*2 + 0] = gx;
                contourBuf[i*2 + 1] = gy;
                cx += gx; cy += gy;
            }
            cx /= 5.0f; cy /= 5.0f;

            // Temporal filtering
            state.leftFilter->filter(contourBuf, timestampSec);
            state.leftCenter.filter(cx, cy, timestampSec);

            for (int i = 0; i < 5; ++i) {
                face.iris.leftContour[i] = { contourBuf[i*2+0], contourBuf[i*2+1] };
            }
            face.iris.leftCenter = { cx, cy };

            // Radius: estimate as max distance from center to contour points
            float maxR = 0;
            for (int i = 0; i < 5; ++i) {
                float dx = contourBuf[i*2+0] - cx;
                float dy = contourBuf[i*2+1] - cy;
                maxR = std::max(maxR, std::hypot(dx, dy));
            }
            face.iris.leftRadius = state.leftRadius.filter(maxR, timestampSec);
            anySuccess = true;
        }
    }

    // ---- RIGHT EYE pass (mirrored) ----
    //
    // MediaPipe Iris was trained on the left eye. To detect the right eye
    // we feed a horizontally mirrored crop, then mirror output X coords back
    // to image-aligned space. This works because the iris is bilaterally
    // symmetric — the model sees what looks like a left eye.
    {
        EyeBounds b = computeEyeBounds(face.landmarks, kRightEyeOuter, kRightEyeInner);

        CameraInputRect crop;
        crop.x = (int)std::max(0.0f, (b.cx - b.halfSize) * imageWidth);
        crop.y = (int)std::max(0.0f, (b.cy - b.halfSize) * imageHeight);
        crop.width  = (int)std::min((float)imageWidth  - crop.x, b.halfSize * 2 * imageWidth);
        crop.height = (int)std::min((float)imageHeight - crop.y, b.halfSize * 2 * imageHeight);
        crop.rotationDeg = 0;
        crop.mirrorX = true;    // KEY: mirror the crop so the model sees a "left eye"

        impl_->model->setInputTexture(0, cameraTex, crop);
        if (impl_->model->run()) {
            impl_->model->readOutput(0, impl_->outputBuf.data(),
                                     impl_->outputBuf.size() * sizeof(float));

            float contourBuf[10];
            float cx = 0, cy = 0;
            for (int i = 0; i < 5; ++i) {
                float lx_mirrored = impl_->outputBuf[i*2 + 0];
                float ly          = impl_->outputBuf[i*2 + 1];

                // Un-mirror the X coordinate: model output is in mirrored
                // crop space, so x' = 1 - x recovers the original orientation.
                float lx = 1.0f - lx_mirrored;

                float gx = (b.cx - b.halfSize) + lx * (b.halfSize * 2);
                float gy = (b.cy - b.halfSize) + ly * (b.halfSize * 2);
                contourBuf[i*2 + 0] = gx;
                contourBuf[i*2 + 1] = gy;
                cx += gx; cy += gy;
            }
            cx /= 5.0f; cy /= 5.0f;

            state.rightFilter->filter(contourBuf, timestampSec);
            state.rightCenter.filter(cx, cy, timestampSec);

            for (int i = 0; i < 5; ++i) {
                face.iris.rightContour[i] = { contourBuf[i*2+0], contourBuf[i*2+1] };
            }
            face.iris.rightCenter = { cx, cy };

            float maxR = 0;
            for (int i = 0; i < 5; ++i) {
                float dx = contourBuf[i*2+0] - cx;
                float dy = contourBuf[i*2+1] - cy;
                maxR = std::max(maxR, std::hypot(dx, dy));
            }
            face.iris.rightRadius = state.rightRadius.filter(maxR, timestampSec);
            anySuccess = true;
        }
    }

    face.iris.valid = anySuccess;
    return anySuccess;
}

void IrisLandmarker::resetTracking() {
    impl_->trackStates.clear();
}

void IrisLandmarker::retainOnly(const std::vector<int>& activeFaceIds) {
    std::unordered_set<int> active(activeFaceIds.begin(), activeFaceIds.end());
    for (auto it = impl_->trackStates.begin(); it != impl_->trackStates.end(); ) {
        if (active.find(it->first) == active.end()) {
            it = impl_->trackStates.erase(it);
        } else {
            ++it;
        }
    }
}

void IrisLandmarker::setFilterParams(float minCutoff, float beta, float dCutoff) {
    impl_->minCutoff = minCutoff;
    impl_->beta = beta;
    impl_->dCutoff = dCutoff;
    for (auto& kv : impl_->trackStates) {
        if (kv.second.leftFilter)
            kv.second.leftFilter->setParams(minCutoff, beta, dCutoff);
        if (kv.second.rightFilter)
            kv.second.rightFilter->setParams(minCutoff, beta, dCutoff);
        kv.second.leftCenter.setParams(minCutoff, beta, dCutoff);
        kv.second.rightCenter.setParams(minCutoff, beta, dCutoff);
        kv.second.leftRadius.setParams(minCutoff, beta * 0.5f, dCutoff);
        kv.second.rightRadius.setParams(minCutoff, beta * 0.5f, dCutoff);
    }
}

}  // namespace community_ar
