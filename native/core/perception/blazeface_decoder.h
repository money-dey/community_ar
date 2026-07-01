// blazeface_decoder.h
// =============================================================================
// Community AR — BlazeFace short-range output decoder
//
// Decodes the raw output of MediaPipe's BlazeFace short-range detector
// (face_detection_short_range.tflite) into a list of face bounding boxes.
//
// Pipeline:
//   1. Generate 896 SSD anchors deterministically from the MediaPipe-published
//      short-range config (done once at initialize() time).
//   2. Decode model output: dy/dx/dh/dw deltas + 6 keypoints per anchor.
//   3. Sigmoid + threshold the confidence scores.
//   4. Weighted non-max suppression (BlazeFace's idiom — averages overlapping
//      detections instead of dropping them, which is why it's "BlazeFace"
//      not standard SSD).
//
// Source of truth: mediapipe/modules/face_detection/face_detection_short_range_common.pbtxt
// =============================================================================

#pragma once

#include "face_tracker.h"   // for DetectedFace
#include <vector>

namespace community_ar {

// -----------------------------------------------------------------------------
// BlazeFaceConfig — must match the model file's training config exactly.
// These values are for the SHORT RANGE variant (128x128, 896 anchors).
// -----------------------------------------------------------------------------
struct BlazeFaceConfig {
    int   inputWidth        = 128;
    int   inputHeight       = 128;
    int   numLayers         = 4;
    int   strides[4]        = {8, 16, 16, 16};
    float minScale          = 0.1484375f;
    float maxScale          = 0.75f;
    float anchorOffsetX     = 0.5f;
    float anchorOffsetY     = 0.5f;
    float interpScaleAR     = 1.0f;
    bool  fixedAnchorSize   = true;

    // Decoding scale factors for box deltas (the model was trained against
    // these). Match the .pbtxt's tensors_to_detections options.
    float xScale = 128.0f;
    float yScale = 128.0f;
    float wScale = 128.0f;
    float hScale = 128.0f;

    // Postprocess thresholds
    float minScoreThreshold = 0.6f;   // post-sigmoid
    float nmsIouThreshold   = 0.3f;
    int   maxOutputDetections = 8;
};

// -----------------------------------------------------------------------------
// BlazeFace anchor — (x, y) in normalized [0,1] image coords
// -----------------------------------------------------------------------------
struct BlazeFaceAnchor {
    float x, y;
    float w, h;   // when fixedAnchorSize=true these are equal at each layer
};

// -----------------------------------------------------------------------------
// generateBlazeFaceAnchors
//
// Produces the deterministic 896-anchor table from the config. Called once
// per session. The output vector has exactly 896 entries for the short-range
// configuration.
// -----------------------------------------------------------------------------
std::vector<BlazeFaceAnchor> generateBlazeFaceAnchors(const BlazeFaceConfig& cfg);

// -----------------------------------------------------------------------------
// decodeBlazeFaceOutput
//
// Inputs:
//   boxesTensor:  pointer to (N * 16) floats. Layout per anchor:
//                 [dy, dx, dh, dw, kp0_y, kp0_x, kp1_y, kp1_x, ..., kp5_y, kp5_x]
//                 (MediaPipe's TENSORS_TO_DETECTIONS uses Y-first ordering)
//   scoresTensor: pointer to (N) floats — raw (pre-sigmoid) class scores
//   anchors:      the table from generateBlazeFaceAnchors()
//   cfg:          the same config used to build the anchors
//
// Returns: up to cfg.maxOutputDetections faces, sorted by score descending.
//          DetectedFace.{x,y,w,h} are in normalized image coords.
// -----------------------------------------------------------------------------
std::vector<DetectedFace> decodeBlazeFaceOutput(
    const float* boxesTensor,
    const float* scoresTensor,
    const std::vector<BlazeFaceAnchor>& anchors,
    const BlazeFaceConfig& cfg);

}  // namespace community_ar
