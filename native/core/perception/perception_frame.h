// perception_frame.h
// =============================================================================
// Community AR — PerceptionFrame
//
// The output of the perception pipeline for a single frame: all landmarks,
// masks, poses, and derived per-face statistics. Read-only from effects'
// perspective. Lives on the render thread; lifetime = one frame.
// =============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace community_ar {

// 2D point in normalized image coords ([0,1] in each axis)
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

class TextureHandle;  // forward

// -----------------------------------------------------------------------------
// FaceLandmarks — output of MediaPipe FaceMesh, post One-Euro filtering
//
// All 468 landmarks are in normalized image coordinates. The third component
// of FaceMesh's output (relative depth) is preserved as `relativeZ` though
// we rarely use it directly.
// -----------------------------------------------------------------------------
struct FaceLandmarks {
    static constexpr int kCount = 468;
    std::array<Vec2, kCount> points;       // smoothed 2D positions
    std::array<float, kCount> relativeZ;   // raw relative depth (signed)

    // Convenience accessors for canonical FaceMesh subsets.
    // These return indices into `points` — actual indices come from MediaPipe.
    // (Kept abstract here; the .cpp pulls them from a shared constants header.)
    const std::vector<int>& lipOuterContour() const;
    const std::vector<int>& lipInnerContour() const;
    const std::vector<int>& leftEyeContour() const;
    const std::vector<int>& rightEyeContour() const;
    const std::vector<int>& leftEyebrow() const;
    const std::vector<int>& rightEyebrow() const;
    const std::vector<int>& noseContour() const;
    const std::vector<int>& faceOval() const;
};

// -----------------------------------------------------------------------------
// IrisLandmarks — output of MediaPipe Iris (one set per eye when available)
// -----------------------------------------------------------------------------
struct IrisLandmarks {
    bool valid = false;
    Vec2 leftCenter;
    Vec2 rightCenter;
    float leftRadius;     // in normalized image coords
    float rightRadius;
    // Optionally: contour points around each iris (5 per eye in MediaPipe Iris)
    std::array<Vec2, 5> leftContour;
    std::array<Vec2, 5> rightContour;
};

// -----------------------------------------------------------------------------
// FacePose — 6DoF pose computed via PnP from face landmarks against a
// canonical 3D face model. Used by AssetOverlay effects to anchor 3D objects
// (glasses, caps, earrings) to the face.
// -----------------------------------------------------------------------------
struct FacePose {
    bool valid = false;
    float rotation[9];      // 3x3 row-major
    float translation[3];   // in camera space
    float confidence;       // 0..1
};

// -----------------------------------------------------------------------------
// SkinToneEstimate — see Pass 5 (v2) of the beautify pipeline
// -----------------------------------------------------------------------------
struct SkinToneEstimate {
    float baselineLuma;     // trimmed mean luminance of skin region
    float baselineChromaR;
    float baselineChromaG;
    float baselineChromaB;
    bool  valid = false;
};

// -----------------------------------------------------------------------------
// FaceData — everything we know about a single detected face this frame
// -----------------------------------------------------------------------------
struct FaceData {
    int faceId;             // stable across frames as long as tracking holds
    FaceLandmarks   landmarks;
    IrisLandmarks   iris;       // may be invalid if iris model didn't run
    FacePose        pose;       // may be invalid if PnP didn't converge
    SkinToneEstimate skinTone;  // may be invalid in early frames

    // 52 ARKit-style blendshape coefficients from MediaPipe FaceLandmarker.
    // Drives expression-aware effect animation (e.g. snouts that open with
    // the user's mouth). Index convention matches Apple's ARKit
    // ARFaceAnchor.BlendShapeLocation ordering; index 25 = jawOpen.
    std::array<float, 52> blendShapes = {};

    // Per-face motion: average landmark displacement vs previous frame,
    // normalized by face size. Used by temporal stabilization.
    float motion = 0.0f;

    // Mouth-open detection (derived from blendShapes[25] for convenience).
    float mouthOpenness = 0.0f;  // 0=closed, 1=wide open
};

// -----------------------------------------------------------------------------
// PerceptionFrame — top-level per-frame perception output
// -----------------------------------------------------------------------------
struct PerceptionFrame {
    int64_t frameId;
    int64_t captureTimestampNs;

    int imageWidth;
    int imageHeight;

    std::vector<FaceData> faces;     // 0..maxFaces detected this frame

    // Whole-image masks (when their corresponding effects requested them)
    const TextureHandle* hairMask   = nullptr;
    const TextureHandle* teethMask  = nullptr;
    const TextureHandle* beardMask  = nullptr;
};

}  // namespace community_ar
