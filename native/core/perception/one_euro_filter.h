// one_euro_filter.h
// =============================================================================
// Community AR — One-Euro filter
//
// Reference: Casiez, Roussel, Vogel — "1€ Filter: A Simple Speed-based
// Low-pass Filter for Noisy Input in Interactive Systems" (CHI 2012).
//
// The problem this solves: raw output from face landmark models is noisy at
// the sub-pixel level. A constant low-pass filter would smooth the jitter
// but introduce noticeable lag when the user moves quickly.
//
// One-Euro adaptively varies the cutoff frequency based on signal speed:
//   - When the signal is moving slowly (steady face), use a low cutoff
//     (aggressive smoothing) — eliminates jitter at rest.
//   - When the signal is moving fast (head turn), use a high cutoff
//     (light smoothing) — preserves responsiveness during motion.
//
// The math is just two cascaded exponential filters with the cutoff of the
// position filter modulated by the magnitude of the velocity filter.
//
// Parameters that work well for face landmarks at 30fps:
//   minCutoff = 1.0   (minimum cutoff frequency in Hz)
//   beta      = 0.007 (cutoff slope: how aggressively to increase cutoff
//                      when velocity rises)
//   dCutoff   = 1.0   (velocity filter cutoff)
//
// Higher minCutoff → less smoothing overall but more responsive
// Higher beta      → faster reaction to motion
// =============================================================================

#pragma once

#include <vector>

namespace community_ar {

class OneEuroFilter1D {
public:
    OneEuroFilter1D(float minCutoff = 1.0f, float beta = 0.007f,
                    float dCutoff = 1.0f);

    float filter(float value, float timestampSec);

    void reset();

    void setParams(float minCutoff, float beta, float dCutoff) {
        minCutoff_ = minCutoff; beta_ = beta; dCutoff_ = dCutoff;
    }

private:
    float lowPass(float& state, float value, float alpha);
    float alphaFromCutoff(float cutoff, float dt) const;

    float minCutoff_, beta_, dCutoff_;
    float prevValue_  = 0.0f;
    float prevDValue_ = 0.0f;
    float prevTime_   = 0.0f;
    bool  initialized_ = false;
};

// -----------------------------------------------------------------------------
// OneEuroFilter2D — convenience wrapper for x/y pairs (face landmark coords)
// -----------------------------------------------------------------------------
class OneEuroFilter2D {
public:
    OneEuroFilter2D(float minCutoff = 1.0f, float beta = 0.007f,
                    float dCutoff = 1.0f)
        : fx_(minCutoff, beta, dCutoff), fy_(minCutoff, beta, dCutoff) {}

    void filter(float& x, float& y, float timestampSec) {
        x = fx_.filter(x, timestampSec);
        y = fy_.filter(y, timestampSec);
    }

    void reset() { fx_.reset(); fy_.reset(); }

    void setParams(float minCutoff, float beta, float dCutoff) {
        fx_.setParams(minCutoff, beta, dCutoff);
        fy_.setParams(minCutoff, beta, dCutoff);
    }

private:
    OneEuroFilter1D fx_, fy_;
};

// -----------------------------------------------------------------------------
// LandmarkArrayFilter — one filter per landmark, applied to all 468 face
// landmarks (or any other fixed-size landmark set).
// -----------------------------------------------------------------------------
class LandmarkArrayFilter {
public:
    explicit LandmarkArrayFilter(int count,
                                 float minCutoff = 1.0f,
                                 float beta = 0.007f,
                                 float dCutoff = 1.0f);

    // In-place filter on an array of (x, y) pairs.
    // points: pointer to 2*count interleaved floats: x0,y0,x1,y1,...
    void filter(float* points, float timestampSec);

    void reset();

    void setParams(float minCutoff, float beta, float dCutoff);

private:
    std::vector<OneEuroFilter2D> filters_;
};

}  // namespace community_ar
