// face_tracker.h
// =============================================================================
// Community AR — Face tracker
//
// The problem: MediaPipe outputs N face detections per frame, but the order
// in which faces appear is not stable. Frame K might detect faces A,B in
// slots [0,1]; frame K+1 might emit them in [1,0]. Without tracking, per-face
// state (One-Euro filters, skin tone, motion estimates) gets scrambled, and
// effects visibly jitter or swap between faces.
//
// What this module does:
//   1. Receives raw face bounding boxes per frame
//   2. Maintains "tracks" — entries with stable IDs, last-seen bounding box,
//      and a small history
//   3. On each new frame, assigns detections to existing tracks via the
//      Hungarian algorithm on IoU (Intersection over Union)
//   4. Spawns new tracks for unmatched detections, retires tracks that
//      haven't been seen for K frames
//   5. Returns a (faceId, detectionIndex) mapping for the current frame
//
// The algorithm is O(N^3) for N faces. We typically run with N≤4, so this
// is negligible (< 0.05ms).
// =============================================================================

#pragma once

#include <cstdint>
#include <vector>

namespace community_ar {

struct DetectedFace {
    // Normalized bounding box [0,1]
    float x, y, w, h;
    // Optional: detection confidence
    float confidence = 1.0f;
};

struct FaceAssignment {
    int detectionIndex;   // index into the current frame's detection array
    int trackId;          // stable across frames; -1 if new track
    bool isNew;           // true on the frame the track was created
};

class FaceTracker {
public:
    struct Config {
        // Minimum IoU between detection and existing track to count as match.
        // Lower = more lenient (allows faster motion); higher = stricter.
        float matchIouThreshold = 0.3f;

        // How many consecutive frames a track can be missed before retiring.
        int maxAge = 5;

        // Maximum simultaneous tracks. Older tracks evicted by age.
        int maxTracks = 4;
    };

    explicit FaceTracker(const Config& cfg = Config{});
    ~FaceTracker();

    // Process the current frame's detections and return per-detection
    // track IDs. The returned vector has one entry per detection, in the
    // same order as the input.
    std::vector<FaceAssignment> update(const std::vector<DetectedFace>& detections);

    // Clear all tracking state. Call when the camera switches or session
    // resumes after a long pause.
    void reset();

    // Diagnostic: number of active (non-retired) tracks
    int activeTrackCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace community_ar
