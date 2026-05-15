// one_euro_filter.cpp
#include "one_euro_filter.h"
#include <cmath>
#include <algorithm>

namespace community_ar {

namespace {
    constexpr float kTwoPi = 6.28318530717958647692f;
}

// -----------------------------------------------------------------------------
// OneEuroFilter1D
// -----------------------------------------------------------------------------
OneEuroFilter1D::OneEuroFilter1D(float minCutoff, float beta, float dCutoff)
    : minCutoff_(minCutoff), beta_(beta), dCutoff_(dCutoff) {}

void OneEuroFilter1D::reset() {
    initialized_ = false;
    prevValue_ = 0.0f;
    prevDValue_ = 0.0f;
    prevTime_ = 0.0f;
}

float OneEuroFilter1D::alphaFromCutoff(float cutoff, float dt) const {
    // tau = 1 / (2pi * cutoff); alpha = 1 / (1 + tau/dt)
    float tau = 1.0f / (kTwoPi * cutoff);
    return 1.0f / (1.0f + tau / dt);
}

float OneEuroFilter1D::lowPass(float& state, float value, float alpha) {
    state = alpha * value + (1.0f - alpha) * state;
    return state;
}

float OneEuroFilter1D::filter(float value, float timestampSec) {
    if (!initialized_) {
        prevValue_ = value;
        prevDValue_ = 0.0f;
        prevTime_ = timestampSec;
        initialized_ = true;
        return value;
    }

    float dt = std::max(timestampSec - prevTime_, 1e-6f);
    prevTime_ = timestampSec;

    // 1. Derivative of the signal
    float dValue = (value - prevValue_) / dt;

    // 2. Smooth the derivative with a constant-cutoff low-pass
    float aD = alphaFromCutoff(dCutoff_, dt);
    float smoothedD = lowPass(prevDValue_, dValue, aD);

    // 3. Cutoff frequency for the position filter scales with |velocity|
    float cutoff = minCutoff_ + beta_ * std::fabs(smoothedD);

    // 4. Smooth the position with the adaptive cutoff
    float aPos = alphaFromCutoff(cutoff, dt);
    return lowPass(prevValue_, value, aPos);
}

// -----------------------------------------------------------------------------
// LandmarkArrayFilter
// -----------------------------------------------------------------------------
LandmarkArrayFilter::LandmarkArrayFilter(int count,
                                         float minCutoff, float beta, float dCutoff)
    : filters_(count, OneEuroFilter2D(minCutoff, beta, dCutoff)) {}

void LandmarkArrayFilter::filter(float* points, float timestampSec) {
    for (size_t i = 0; i < filters_.size(); ++i) {
        float x = points[i * 2 + 0];
        float y = points[i * 2 + 1];
        filters_[i].filter(x, y, timestampSec);
        points[i * 2 + 0] = x;
        points[i * 2 + 1] = y;
    }
}

void LandmarkArrayFilter::reset() {
    for (auto& f : filters_) f.reset();
}

void LandmarkArrayFilter::setParams(float minCutoff, float beta, float dCutoff) {
    for (auto& f : filters_) f.setParams(minCutoff, beta, dCutoff);
}

}  // namespace community_ar
