// skin_smooth_effect.h
// =============================================================================
// Community AR — SkinSmoothEffect (the beauty v2 engine)
//
// Implements the 9-pass multi-band frequency separation skin beautification
// pipeline described in car-phase-3-requirements.md (Decision 4).
//
// In Batch 3 (this file), SkinSmoothEffect is a SCAFFOLD:
//   - Class structure, configuration, graph integration: complete
//   - passOrder, perception requirements, mask requirements: declared
//   - Renders as PASSTHROUGH for now — copies input to output, no shaders
//
// In Batch 4, the nine fragment shaders fill in:
//   P1   skin mask refinement       — combines segmenter mask + landmarks
//   P2   13-tap downsample          — to half-resolution
//   P3a  bilateral H, small radius  — mid-frequency preservation
//   P3b  bilateral V, small radius
//   P3c  bilateral H, large radius  — low-frequency baseline
//   P3d  bilateral V, large radius
//   P4   tent upsample              — back to full resolution
//   P5   multi-band Oklab composition  — the heart of the algorithm
//   P5.5 specular control           — matte/glow continuum
//   P6   glow finishing             — warmth, highlight lift, clarity
//   P6.5 temporal stabilization     — motion-gated history blend
//
// Mask resource contract (exercises pool produce-side API):
//   - Consumes:  masks.faceSkin
//                (or masks.faceLandmarkSkin if the multiclass model wasn't
//                 active; SkinSmoothEffect handles both with priority)
//   - Produces:  masks.refinedFaceSkin
//                (P1's output, available to later effects that want a
//                 better-than-segmenter skin mask)
//
// Quality tiers:
//   Auto-selected at first activation (Q2 in car-phase-3-dag.md). The tier
//   determines which passes are skipped:
//   - High:   all 11 passes (P1, P2, P3a-d, P4, P5, P5.5, P6, P6.5)
//   - Medium: skip P3c/P3d (single bilateral band, no low-freq baseline)
//   - Low:    skip P3c/P3d AND P5.5; bilateral at quarter-res
//
// Verification status (May 2026):
//   - Scaffold: designed, integrates with effect graph
//   - Shaders: pending Batch 4
//   - On-device benchmarks: pending real hardware testing
// =============================================================================

#pragma once

#include "effect_base.h"
#include "effect_pass.h"
#include "../ffi/community_ar_phase3_api.h"  // CARBeautyFilterConfig
#include <array>
#include <memory>

namespace community_ar {

class TextureHandle;
class Framebuffer;
class ShaderProgram;

// -----------------------------------------------------------------------------
// BeautyQuality — runtime tier selection
// -----------------------------------------------------------------------------
enum class BeautyQuality : uint32_t {
    Auto   = 0,   // Pick at first activation based on benchmark
    High   = 1,   // All 11 passes
    Medium = 2,   // Skip P3c/P3d
    Low    = 3,   // Skip P3c/P3d + P5.5, bilateral at quarter-res
};

// -----------------------------------------------------------------------------
// SkinSmoothEffect — the beauty v2 engine
// -----------------------------------------------------------------------------
class SkinSmoothEffect : public Effect {
public:
    // Config struct — same layout as CARBeautyFilterConfig but with the
    // BeautyQuality enum unpacked. Constructed by the factory.
    struct Config {
        // All fields documented in car-phase-3-requirements.md Decision 5
        // and in lib/src/effects/beauty_filter_config.dart.
        float smoothingStrength       = 0.7f;
        float detailPreserve          = 0.8f;
        float blemishReduction        = 0.6f;
        float bilateralEdgeSensitivity = 0.15f;
        float highFreqStrength        = 0.9f;
        float midFreqStrength         = 0.5f;
        float warmth                  = 0.04f;
        float highlightLift           = 0.08f;
        float clarity                 = 0.15f;
        float specularControl         = 0.0f;
        float temporalSmoothing       = 0.7f;
        float adaptivenessLocal       = 0.5f;
        BeautyQuality quality         = BeautyQuality::Auto;
    };

    explicit SkinSmoothEffect(Config cfg);
    ~SkinSmoothEffect() override;

    // ---- Effect interface ----
    uint32_t typeId() const override;  // returns CAR_EFFECT_SKIN_SMOOTH (=8)
    EffectPass passOrder() const override { return EffectPass::SkinAdjust; }

    PerceptionInputs perceptionInputs() const override;
    MaskRequirements maskRequirements() const override;

    void prepare(const PerceptionFrame& frame,
                 MaskResourcePool& maskPool,
                 RenderContext* ctx) override;

    void render(const TextureHandle& inputTex,
                Framebuffer* outputFbo,
                const MaskResourcePool& maskPool,
                RenderContext* ctx) override;

    // ---- Quality tier inspection ----
    // Returns the effective tier (resolves Auto → High/Medium/Low after
    // the benchmark has run). For diagnostics.
    BeautyQuality effectiveQuality() const { return effectiveQuality_; }

private:
    // Ensure GPU resources (shaders, intermediate FBOs) are allocated.
    // Lazy — runs on the first prepare() / render() call so we don't pay
    // for resources before the user activates beauty.
    void ensureResources(int width, int height, RenderContext* ctx);

    // Quality tier resolution. Runs on first activation if quality == Auto;
    // otherwise immediately returns the user-specified tier.
    void resolveAutoTier();

    // Specular animation easing (small, localized — per our scope decision
    // in the requirements doc). Updates currentSpecular_ toward
    // cfg_.specularControl over ~5 frames.
    void easeSpecular();

    // Populate the per-frame state struct from a PerceptionFrame. Picks
    // the dominant face for baselineLuma/motion. Called by prepare().
    void capturePerceptionState(const PerceptionFrame& frame);

    // Auto-tier benchmark machinery. Both run only when cfg_.quality == Auto.
    //   recordFrameTime — call at end of render() with measured frame_ms
    //   adaptiveThrottle — call after benchmark is locked; may downgrade
    //                       or restore tier based on rolling perf
    void recordFrameTime(float frameMs);
    void adaptiveThrottle(float frameMs);

    // ---- Scaffold render: passthrough ----
    // Falls back to this when resources aren't ready or no face is visible.
    void renderPassthrough(const TextureHandle& inputTex,
                           Framebuffer* outputFbo,
                           RenderContext* ctx);

    Config cfg_;
    BeautyQuality effectiveQuality_ = BeautyQuality::Auto;

    // Resolved-at-runtime state
    bool   resourcesReady_ = false;
    int    resourceWidth_  = 0;
    int    resourceHeight_ = 0;

    // Frame counter (used for benchmark and animation)
    int    framesSinceActivation_ = 0;

    // Specular easing state
    float  currentSpecular_ = 0.0f;
    static constexpr float kSpecularEaseRate = 0.2f;  // ~5 frame ease

    // Per-frame state captured in prepare(), consumed by render()
    struct FrameState {
        float baselineLuma   = 0.5f;   // dominant face's skin tone baseline
        float motionMagnitude = 0.0f;  // dominant face's motion estimate
        int   visibleFaceCount = 0;    // for multi-face tier adjustment
        bool  hasFaces = false;        // false → passthrough render
    };
    FrameState frameState_;

    // Auto-tier benchmark state. Used only when cfg_.quality == Auto.
    static constexpr int   kBenchmarkFrames = 10;
    static constexpr float kHighTierMaxFrameMs   = 22.0f;
    static constexpr float kMediumTierMaxFrameMs = 26.0f;
    static constexpr float kThrottleTriggerMs    = 30.0f;
    static constexpr int   kThrottleConsecutive  = 3;
    static constexpr int   kRestoreHeadroomFrames = 100;

    bool  benchmarkComplete_ = false;
    float benchmarkTotalMs_ = 0.0f;
    int   benchmarkFrameCount_ = 0;

    // Adaptive throttling: count consecutive over-budget / under-budget frames
    int   overBudgetStreak_  = 0;
    int   underBudgetStreak_ = 0;

    // ---- Batch 4 placeholders ----
    // Shader programs for each pass. Allocated in ensureResources()
    // when Batch 4 lands. Held as unique_ptrs so they're optional in scaffold.
    std::unique_ptr<ShaderProgram> shaderP1_;
    std::unique_ptr<ShaderProgram> shaderP2_;
    std::unique_ptr<ShaderProgram> shaderP3_;    // bilateral, used for P3a-d
    std::unique_ptr<ShaderProgram> shaderP4_;
    std::unique_ptr<ShaderProgram> shaderP5_;
    std::unique_ptr<ShaderProgram> shaderP55_;
    std::unique_ptr<ShaderProgram> shaderP6_;
    std::unique_ptr<ShaderProgram> shaderP65_;

    // Intermediate textures + FBOs for the multi-pass pipeline.
    // Allocated in ensureResources() at scaled sizes:
    //   skinMask:        full res, R8         (P1 output, published to pool)
    //   halfRes[0..2]:   half res, RGBA8      (P3 ping-pong + mid-band hold)
    //   upsample[0..1]:  full res, RGBA8      (P4 mid/low outputs, then post-P5)
    //   prevOutput:      full res, RGBA8      (temporal history buffer)
    //
    // Three half-res buffers (not two): the bilateral passes need to
    // preserve the mid-band result while computing the low-band, which
    // requires holding the original downsample AND the mid-band
    // simultaneously. See skin_smooth_effect.cpp render() for the data flow.
    std::unique_ptr<TextureHandle> texSkinMask_;
    std::unique_ptr<Framebuffer>   fboSkinMask_;
    std::array<std::unique_ptr<TextureHandle>, 3> texHalfRes_;
    std::array<std::unique_ptr<Framebuffer>,   3> fboHalfRes_;
    std::array<std::unique_ptr<TextureHandle>, 2> texUpsample_;
    std::array<std::unique_ptr<Framebuffer>,   2> fboUpsample_;
    std::unique_ptr<TextureHandle> texPrevOutput_;
    std::unique_ptr<Framebuffer>   fboPrevOutput_;
};

}  // namespace community_ar
