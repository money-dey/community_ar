// pnp_solver.h
// =============================================================================
// Community AR — PnP face pose solver
//
// Given 2D face landmarks (image pixel coords) and a canonical 3D face model
// (the same canonical points that MediaPipe FaceMesh was trained against),
// recover the camera-relative 6DoF pose of the face.
//
// We use a subset of stable landmarks (corners of eyes, nose tip, chin,
// mouth corners) — typically ~6–9 points — rather than all 468, because:
//   - Fewer points = faster solve
//   - Stable landmarks (not eyelid/lip contour) give better numerical
//     conditioning
//
// Algorithm: Iterative Gauss-Newton on the reprojection error, initialized
// by EPnP (Efficient PnP). For most face-tracking purposes a few iterations
// converge to sub-pixel reprojection error.
//
// In practice we avoid pulling in OpenCV just for solvePnP — we implement
// a minimal EPnP + 3-iteration Gauss-Newton refinement inline. ~150 lines
// of dense numerical code, but tractable.
// =============================================================================

#pragma once

#include <array>

namespace community_ar {

struct CameraIntrinsics {
    float fx, fy;        // focal length in pixels
    float cx, cy;        // principal point in pixels
    int   imageWidth;
    int   imageHeight;

    // Convenience: build approximate intrinsics from image dimensions when
    // exact calibration isn't available. Phone cameras typically have an
    // effective FOV around 65–75 degrees on the long axis.
    static CameraIntrinsics approximate(int width, int height,
                                        float horizontalFovDeg = 70.0f);
};

struct PnPResult {
    bool   valid = false;
    float  rotation[9];    // row-major 3x3
    float  translation[3]; // camera space
    float  reprojectionError; // RMS in pixels
};

// -----------------------------------------------------------------------------
// Canonical face model — a tiny set of stable 3D points (in face-local space,
// units of mm with origin between the eyes). These match MediaPipe FaceMesh
// landmark indices.
// -----------------------------------------------------------------------------
struct CanonicalFacePoint {
    int   landmarkIndex;   // index into the 468-point FaceMesh array
    float x, y, z;         // 3D position in canonical face-local mm
};

// The stable subset we use for PnP. Empirically chosen for numerical stability
// and consistency across face shapes.
const CanonicalFacePoint* getCanonicalFacePoints(int* outCount);

// -----------------------------------------------------------------------------
// Solver
//
// Inputs:
//   landmarks2d     — pointer to 2*N interleaved floats (x,y) in pixel coords
//   numLandmarks    — total landmarks in the array (468 for FaceMesh)
//   intrinsics      — camera intrinsics (pixel-space)
//
// Output: a PnPResult. valid == false if the solve failed.
// -----------------------------------------------------------------------------
PnPResult solveFacePose(const float* landmarks2d,
                        int numLandmarks,
                        const CameraIntrinsics& intrinsics);

}  // namespace community_ar
