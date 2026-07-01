// blazeface_decoder.cpp
// =============================================================================
// Production BlazeFace short-range decoder.
//
// Mirrors MediaPipe's SsdAnchorsCalculator + TensorsToDetectionsCalculator +
// NonMaxSuppressionCalculator (weighted variant) for the short-range model.
// Cross-checked against face_detection_short_range_common.pbtxt and the
// SsdAnchorsCalculator implementation in MediaPipe's source.
//
// For the 4-layer, 128x128 short-range model, this generates exactly 896
// anchors — matching the model's output dimension of (1, 896, 16/1).
// =============================================================================

#include "blazeface_decoder.h"
#include <algorithm>
#include <cmath>

namespace community_ar {

namespace {

inline float sigmoid(float x) {
    // Clamp before exp() to avoid overflow at extreme negatives
    if (x < -16.0f) return 0.0f;
    if (x >  16.0f) return 1.0f;
    return 1.0f / (1.0f + std::exp(-x));
}

inline float computeIoU(const DetectedFace& a, const DetectedFace& b) {
    float ax2 = a.x + a.w, ay2 = a.y + a.h;
    float bx2 = b.x + b.w, by2 = b.y + b.h;
    float ix1 = std::max(a.x, b.x);
    float iy1 = std::max(a.y, b.y);
    float ix2 = std::min(ax2, bx2);
    float iy2 = std::min(ay2, by2);
    float iw = std::max(0.0f, ix2 - ix1);
    float ih = std::max(0.0f, iy2 - iy1);
    float inter = iw * ih;
    float uni = a.w * a.h + b.w * b.h - inter;
    return uni > 1e-9f ? inter / uni : 0.0f;
}

// MediaPipe's SsdAnchorsCalculator interpolation rule. Returns the scale value
// for SSD layer `layerId` given total layer count `numLayers` and the
// (minScale, maxScale) range.
inline float interpolateScale(float minScale, float maxScale,
                              int layerId, int numLayers) {
    if (numLayers == 1) return (minScale + maxScale) * 0.5f;
    return minScale + (maxScale - minScale) * (float)layerId /
           (float)(numLayers - 1);
}

}  // anonymous namespace

// -----------------------------------------------------------------------------
// generateBlazeFaceAnchors
//
// Faithful port of MediaPipe's SsdAnchorsCalculator::GenerateAnchors logic,
// specialized for the short-range face detector parameters. We don't pull in
// the full templated calculator — we just unroll the loop for the known config.
// -----------------------------------------------------------------------------
std::vector<BlazeFaceAnchor> generateBlazeFaceAnchors(const BlazeFaceConfig& cfg) {
    std::vector<BlazeFaceAnchor> anchors;
    anchors.reserve(896);

    int layerId = 0;
    while (layerId < cfg.numLayers) {
        // Each "layer" may collapse multiple consecutive stride entries with
        // the same stride value. The short-range config has strides [8,16,16,16]
        // — so layers 1, 2, 3 all share stride=16 and produce anchors at one
        // grid resolution but with multiple sizes per cell.
        std::vector<float> anchorHeight, anchorWidth;
        std::vector<float> aspectRatios;
        std::vector<float> scales;

        int lastSameStrideLayer = layerId;
        while (lastSameStrideLayer < cfg.numLayers &&
               cfg.strides[lastSameStrideLayer] == cfg.strides[layerId]) {
            float scale = interpolateScale(cfg.minScale, cfg.maxScale,
                                           lastSameStrideLayer, cfg.numLayers);
            // One scale × one aspect-ratio per layer for short-range
            scales.push_back(scale);
            aspectRatios.push_back(1.0f);

            // Plus the interpolated-scale anchor: sqrt(s_i * s_{i+1})
            float scaleNext = (lastSameStrideLayer == cfg.numLayers - 1)
                              ? 1.0f
                              : interpolateScale(cfg.minScale, cfg.maxScale,
                                                 lastSameStrideLayer + 1,
                                                 cfg.numLayers);
            scales.push_back(std::sqrt(scale * scaleNext));
            aspectRatios.push_back(cfg.interpScaleAR);

            lastSameStrideLayer++;
        }

        for (size_t i = 0; i < aspectRatios.size(); ++i) {
            float ar = aspectRatios[i];
            float s  = scales[i];
            float sqrtAr = std::sqrt(ar);
            anchorWidth.push_back(s * sqrtAr);
            anchorHeight.push_back(s / sqrtAr);
        }

        // Walk the spatial grid for this stride. featureMap size = ceil(input / stride).
        int stride = cfg.strides[layerId];
        int featureMapH = (cfg.inputHeight + stride - 1) / stride;
        int featureMapW = (cfg.inputWidth  + stride - 1) / stride;

        for (int y = 0; y < featureMapH; ++y) {
            for (int x = 0; x < featureMapW; ++x) {
                for (size_t i = 0; i < anchorHeight.size(); ++i) {
                    BlazeFaceAnchor a;
                    a.x = (float(x) + cfg.anchorOffsetX) / float(featureMapW);
                    a.y = (float(y) + cfg.anchorOffsetY) / float(featureMapH);
                    if (cfg.fixedAnchorSize) {
                        a.w = 1.0f;
                        a.h = 1.0f;
                    } else {
                        a.w = anchorWidth[i];
                        a.h = anchorHeight[i];
                    }
                    anchors.push_back(a);
                }
            }
        }
        layerId = lastSameStrideLayer;
    }

    // For short-range: 256 + 128 + 128 + 128 + ... = 896 (verified at runtime
    // by asserting anchors.size() == 896 in initialize())
    return anchors;
}

// -----------------------------------------------------------------------------
// decodeBlazeFaceOutput
//
// Two stages:
//   1. Per-anchor decoding + threshold (linear pass over all 896)
//   2. Weighted NMS (groups overlapping detections, weighted-averages them)
// -----------------------------------------------------------------------------
std::vector<DetectedFace> decodeBlazeFaceOutput(
        const float* boxesTensor,
        const float* scoresTensor,
        const std::vector<BlazeFaceAnchor>& anchors,
        const BlazeFaceConfig& cfg) {

    std::vector<DetectedFace> candidates;
    candidates.reserve(64);

    const int N = (int)anchors.size();

    // ---- Stage 1: decode each anchor ----
    for (int i = 0; i < N; ++i) {
        float score = sigmoid(scoresTensor[i]);
        if (score < cfg.minScoreThreshold) continue;

        const float* box = boxesTensor + i * 16;
        // MediaPipe layout: y_center, x_center, h, w (note Y-first ordering)
        float dy = box[0] / cfg.yScale * anchors[i].h;
        float dx = box[1] / cfg.xScale * anchors[i].w;
        float dh = box[2] / cfg.hScale * anchors[i].h;
        float dw = box[3] / cfg.wScale * anchors[i].w;

        float cx = anchors[i].x + dx;
        float cy = anchors[i].y + dy;

        DetectedFace f;
        f.x = cx - dw * 0.5f;
        f.y = cy - dh * 0.5f;
        f.w = dw;
        f.h = dh;
        f.confidence = score;

        // Clamp into normalized image bounds (off-frame detections happen
        // at image edges and produce bogus crops downstream)
        if (f.x < 0) { f.w += f.x; f.x = 0; }
        if (f.y < 0) { f.h += f.y; f.y = 0; }
        if (f.x + f.w > 1) f.w = 1.0f - f.x;
        if (f.y + f.h > 1) f.h = 1.0f - f.y;
        if (f.w <= 0 || f.h <= 0) continue;

        candidates.push_back(f);
    }
    if (candidates.empty()) return {};

    // Sort by descending score (NMS consumes in that order)
    std::sort(candidates.begin(), candidates.end(),
              [](const DetectedFace& a, const DetectedFace& b) {
                  return a.confidence > b.confidence;
              });

    // ---- Stage 2: weighted NMS ----
    // For each remaining high-score candidate, group all overlapping detections
    // with IoU > threshold, then output their score-weighted average box.
    // This is MediaPipe's "weighted" NMS, not classic suppression — it produces
    // less jittery boxes for the next frame's tracker to match.
    std::vector<DetectedFace> output;
    std::vector<bool> used(candidates.size(), false);

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (used[i]) continue;
        if ((int)output.size() >= cfg.maxOutputDetections) break;

        float sumW = candidates[i].confidence;
        float wx = candidates[i].x * sumW;
        float wy = candidates[i].y * sumW;
        float ww = candidates[i].w * sumW;
        float wh = candidates[i].h * sumW;
        used[i] = true;

        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (used[j]) continue;
            if (computeIoU(candidates[i], candidates[j]) < cfg.nmsIouThreshold)
                continue;
            float c = candidates[j].confidence;
            wx += candidates[j].x * c;
            wy += candidates[j].y * c;
            ww += candidates[j].w * c;
            wh += candidates[j].h * c;
            sumW += c;
            used[j] = true;
        }

        DetectedFace merged;
        merged.x = wx / sumW;
        merged.y = wy / sumW;
        merged.w = ww / sumW;
        merged.h = wh / sumW;
        merged.confidence = candidates[i].confidence;  // peak, not average
        output.push_back(merged);
    }
    return output;
}

}  // namespace community_ar
