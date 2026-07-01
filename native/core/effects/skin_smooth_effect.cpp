// skin_smooth_effect.cpp  (Phase 3 — Batch 4: full 9-pass pipeline)
// =============================================================================
// SkinSmoothEffect — Batch 4 implementation: the actual 9-pass beauty
// pipeline replaces the Batch 3 passthrough.
//
// Pipeline data flow (High quality tier):
//
//   inputTex ─┬─[P1]──> texSkinMask_ → pool.put("masks.refinedFaceSkin")
//             │
//             ├─[P2]──> halfRes_[0]
//             │           ├─[P3a H small]──> halfRes_[1]
//             │           │   └─[P3b V small]──> halfRes_[2]    (mid-band)
//             │           │
//             │           └─[P3c H large]──> halfRes_[1]
//             │               └─[P3d V large]──> halfRes_[0]    (low-band)
//             │
//             ├─[P4 mid] halfRes_[2] ─> upsample_[0]
//             ├─[P4 low] halfRes_[0] ─> upsample_[1]
//             │
//             ├─[P5: inputTex + upsample_[0] + upsample_[1] + skinMask
//             │     + skinTone] → upsample_[0] (reused, mid-band no longer needed)
//             │
//             ├─[P5.5 specular] upsample_[0] → upsample_[1]
//             ├─[P6 glow]       upsample_[1] → upsample_[0]
//             │
//             └─[P6.5 temporal] upsample_[0] + texPrevOutput_ → outputFbo
//                  └─[history blit] outputFbo → texPrevOutput_
//
// Quality tier deltas:
//   Medium: skip P3c/P3d (no large bilateral); the low-band is taken
//           from a downsampled+upsampled input instead. P5 treats
//           uLowBand and uMidBand as nearly-identical, which collapses
//           to single-band smoothing — still produces nice results,
//           just less control over wrinkle attenuation specifically.
//   Low:    skip P3c/P3d, skip P5.5; bilateral at quarter-resolution
//           (not implemented in Batch 4 — bilateral resolution is a
//           runtime choice that Batch 5 wires up alongside the
//           auto-tier benchmark).
// =============================================================================

#include "skin_smooth_effect.h"
#include "effect_types.h"
#include "mask_resource_pool.h"
#include "../render/render_context.h"
#include "../render/render_context_additions.h"

#include "beauty_shader_common.h"
#include "beauty_shader_p1_skin_mask.h"
#include "beauty_shader_p2_downsample.h"
#include "beauty_shader_p3_bilateral.h"
#include "beauty_shader_p4_upsample.h"
#include "beauty_shader_p5_composition.h"
#include "beauty_shader_p55_specular.h"
#include "beauty_shader_p6_glow.h"
#include "beauty_shader_p65_temporal.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace community_ar {

// -----------------------------------------------------------------------------
// Construction / destruction
// -----------------------------------------------------------------------------
SkinSmoothEffect::SkinSmoothEffect(Config cfg) : cfg_(std::move(cfg)) {
    currentSpecular_ = cfg_.specularControl;
}
SkinSmoothEffect::~SkinSmoothEffect() = default;

uint32_t SkinSmoothEffect::typeId() const {
    return CAR_EFFECT_SKIN_SMOOTH;
}

// -----------------------------------------------------------------------------
// Perception + mask requirements (unchanged from Batch 3)
// -----------------------------------------------------------------------------
PerceptionInputs SkinSmoothEffect::perceptionInputs() const {
    PerceptionInputs in;
    in.needsFaceLandmarks = true;
    in.needsFaceSkinMask  = true;
    in.needsFacePose      = true;
    in.needsSkinTone      = true;
    return in;
}

MaskRequirements SkinSmoothEffect::maskRequirements() const {
    MaskRequirements req;
    req.consumes = { MaskResourcePool::kFaceSkin };
    req.produces = { "masks.refinedFaceSkin" };
    return req;
}

// -----------------------------------------------------------------------------
// ensureResources — Batch 4 adds shader compilation
// -----------------------------------------------------------------------------
void SkinSmoothEffect::ensureResources(int width, int height,
                                        RenderContext* ctx) {
    const bool sizeMatches = (resourceWidth_ == width &&
                              resourceHeight_ == height);
    if (resourcesReady_ && sizeMatches) return;

    auto* ex = static_cast<RenderContextEx*>(ctx);

    // Full-res skin mask (R8)
    texSkinMask_ = ctx->createTexture(width, height, TextureHandle::Format::R8);
    fboSkinMask_ = ex->createFramebufferForTexture(*texSkinMask_);

    // Half-res ping-pong textures (3 of them — needed to preserve mid-band
    // while computing low-band)
    const int halfW = std::max(1, width / 2);
    const int halfH = std::max(1, height / 2);
    for (int i = 0; i < 3; ++i) {
        texHalfRes_[i] = ctx->createTexture(halfW, halfH,
                                            TextureHandle::Format::RGBA8);
        fboHalfRes_[i] = ex->createFramebufferForTexture(*texHalfRes_[i]);
    }

    // Full-res upsample / post-process ping-pong (2 buffers)
    for (int i = 0; i < 2; ++i) {
        texUpsample_[i] = ctx->createTexture(width, height,
                                              TextureHandle::Format::RGBA8);
        fboUpsample_[i] = ex->createFramebufferForTexture(*texUpsample_[i]);
    }

    // Temporal history buffer
    texPrevOutput_ = ctx->createTexture(width, height,
                                         TextureHandle::Format::RGBA8);
    fboPrevOutput_ = ex->createFramebufferForTexture(*texPrevOutput_);

    // Compile shaders if not already compiled. They're size-independent
    // so we only compile on the first call.
    if (!shaderP1_) {
        shaderP1_  = ctx->createShader(kP1VS,  kP1FS);
        shaderP2_  = ctx->createShader(kP2VS,  kP2FS);
        // P3 and P5 need shared Oklab helpers prepended
        const std::string p3fs = makeP3FragmentShader();
        const std::string p5fs = makeP5FragmentShader();
        const std::string p55fs = makeP55FragmentShader();
        const std::string p6fs  = makeP6FragmentShader();
        shaderP3_  = ctx->createShader(kP3VS,  p3fs.c_str());
        shaderP4_  = ctx->createShader(kP4VS,  kP4FS);
        shaderP5_  = ctx->createShader(kP5VS,  p5fs.c_str());
        shaderP55_ = ctx->createShader(kP55VS, p55fs.c_str());
        shaderP6_  = ctx->createShader(kP6VS,  p6fs.c_str());
        shaderP65_ = ctx->createShader(kP65VS, kP65FS);
    }

    resourceWidth_  = width;
    resourceHeight_ = height;
    resourcesReady_ = true;
    // Flag the history buffer as uninitialized — P6.5 first-frame logic
    // will handle this by initializing on first activation.
    framesSinceActivation_ = 0;
}

// -----------------------------------------------------------------------------
// Quality tier resolution (Batch 5 will replace with real benchmark)
// -----------------------------------------------------------------------------
void SkinSmoothEffect::resolveAutoTier() {
    if (cfg_.quality != BeautyQuality::Auto) {
        effectiveQuality_ = cfg_.quality;
        return;
    }
    effectiveQuality_ = BeautyQuality::High;
}

// -----------------------------------------------------------------------------
// Specular easing (unchanged from Batch 3)
// -----------------------------------------------------------------------------
void SkinSmoothEffect::easeSpecular() {
    const float target = cfg_.specularControl;
    const float delta  = target - currentSpecular_;
    if (std::fabs(delta) < 0.001f) {
        currentSpecular_ = target;
        return;
    }
    currentSpecular_ += delta * kSpecularEaseRate;
}

// -----------------------------------------------------------------------------
// prepare — P1 runs here (publishes refined skin mask to pool)
// -----------------------------------------------------------------------------
void SkinSmoothEffect::prepare(const PerceptionFrame& frame,
                                MaskResourcePool& maskPool,
                                RenderContext* ctx) {
    ensureResources(frame.imageWidth, frame.imageHeight, ctx);

    if (framesSinceActivation_ == 0) {
        resolveAutoTier();
    }
    framesSinceActivation_++;

    easeSpecular();

    // Capture per-frame state for render() to consume.
    capturePerceptionState(frame);

    // Multi-face perf strategy (car-phase-3-requirements.md Decision 9):
    // when 3+ faces are visible, drop High → Medium for the frame to
    // avoid quadratic-looking slowdown. We only adjust if Auto-resolved
    // to High — if the user explicitly requested High, respect that.
    if (cfg_.quality == BeautyQuality::Auto &&
        effectiveQuality_ == BeautyQuality::High &&
        frameState_.visibleFaceCount > 2) {
        effectiveQuality_ = BeautyQuality::Medium;
    }

    // ---- P1: refine skin mask ----
    auto faceSkin = maskPool.get(MaskResourcePool::kFaceSkin);
    if (!faceSkin) {
        // Fallback to landmark-derived skin polygon if available
        faceSkin = maskPool.get(MaskResourcePool::kFaceLandmarkSkin);
    }
    if (!faceSkin) {
        // No mask available at all — skip P1, downstream passes
        // will check texSkinMask_ for emptiness and degrade.
        return;
    }

    auto landmarkExcl = maskPool.get("masks.landmarkExclusions");  // optional

    ctx->bindFramebuffer(fboSkinMask_.get());
    ctx->clearColor(0, 0, 0, 1);
    shaderP1_->use();
    shaderP1_->bindTexture("uSegmenterMask", *faceSkin, 0);
    if (landmarkExcl) {
        shaderP1_->bindTexture("uLandmarkExclusions", *landmarkExcl, 1);
        shaderP1_->setUniform("uHasLandmarkExclusions", 1.0f);
    } else {
        shaderP1_->setUniform("uHasLandmarkExclusions", 0.0f);
    }
    shaderP1_->setUniform("uTexelSize",
                          1.0f / (float)frame.imageWidth,
                          1.0f / (float)frame.imageHeight);
    ctx->drawFullscreenQuad(shaderP1_.get());

    // Publish the refined mask to the pool — now it's a real refined
    // mask, not just an alias as in Batch 3.
    std::shared_ptr<TextureHandle> refined(
        texSkinMask_.get(),
        [](TextureHandle*){});  // non-owning shared_ptr (pool clears each frame)
    maskPool.put("masks.refinedFaceSkin", refined);
}

// -----------------------------------------------------------------------------
// Helper for one pass (binds shader, binds inputs, draws)
// -----------------------------------------------------------------------------
namespace {
void runPass(RenderContext* ctx, ShaderProgram* shader, Framebuffer* dst) {
    ctx->bindFramebuffer(dst);
    ctx->drawFullscreenQuad(shader);
}
}

// -----------------------------------------------------------------------------
// render — the 9-pass beauty pipeline
// -----------------------------------------------------------------------------
void SkinSmoothEffect::render(const TextureHandle& inputTex,
                               Framebuffer* outputFbo,
                               const MaskResourcePool& maskPool,
                               RenderContext* ctx) {
    // No faces or shaders not initialized → passthrough.
    if (!shaderP2_ || !frameState_.hasFaces) {
        renderPassthrough(inputTex, outputFbo, ctx);
        return;
    }

    // ---- Frame-time measurement (auto-tier benchmark + adaptive throttle) ----
    // Sub-microsecond overhead; safe to do unconditionally.
    auto frameStart = std::chrono::steady_clock::now();

    // Per-frame perception data — populated in prepare().
    const float baselineLuma    = frameState_.baselineLuma;
    const float motionMagnitude = frameState_.motionMagnitude;

    const int W = resourceWidth_;
    const int H = resourceHeight_;
    const float halfTexelX = 1.0f / (float)(W / 2);
    const float halfTexelY = 1.0f / (float)(H / 2);
    const float fullTexelX = 1.0f / (float)W;
    const float fullTexelY = 1.0f / (float)H;

    const bool useLargeBilateral = (effectiveQuality_ != BeautyQuality::Low &&
                                     effectiveQuality_ != BeautyQuality::Medium);
    const bool useSpecularPass   = (effectiveQuality_ != BeautyQuality::Low);

    // ============== P2: downsample input to half-res ==============
    shaderP2_->use();
    shaderP2_->bindTexture("uSource", inputTex, 0);
    shaderP2_->setUniform("uSourceTexelSize", fullTexelX, fullTexelY);
    runPass(ctx, shaderP2_.get(), fboHalfRes_[0].get());

    // ============== P3a/b: bilateral, small radius (mid-band) ==============
    // halfRes[0] -> halfRes[1] -> halfRes[2]
    shaderP3_->use();
    shaderP3_->setUniform("uBaselineLuma", baselineLuma);
    shaderP3_->setUniform("uEdgeSensitivity", cfg_.bilateralEdgeSensitivity);

    // P3a — horizontal small
    shaderP3_->bindTexture("uSource", *texHalfRes_[0], 0);
    shaderP3_->setUniform("uTexelSize", halfTexelX, halfTexelY);
    shaderP3_->setUniform("uAxis", 1.0f, 0.0f);
    shaderP3_->setUniform("uRadius", 1.0f);
    runPass(ctx, shaderP3_.get(), fboHalfRes_[1].get());

    // P3b — vertical small (output = mid-band)
    shaderP3_->bindTexture("uSource", *texHalfRes_[1], 0);
    shaderP3_->setUniform("uAxis", 0.0f, 1.0f);
    shaderP3_->setUniform("uRadius", 1.0f);
    runPass(ctx, shaderP3_.get(), fboHalfRes_[2].get());

    // ============== P3c/d: bilateral, large radius (low-band) ==============
    // Reuses halfRes[0] (the original downsample) as source for the large
    // bilateral, since it's no longer needed after P3a.
    if (useLargeBilateral) {
        // P3c — horizontal large
        shaderP3_->bindTexture("uSource", *texHalfRes_[0], 0);
        shaderP3_->setUniform("uAxis", 1.0f, 0.0f);
        shaderP3_->setUniform("uRadius", 3.0f);
        runPass(ctx, shaderP3_.get(), fboHalfRes_[1].get());

        // P3d — vertical large (output = low-band, overwrites halfRes[0])
        shaderP3_->bindTexture("uSource", *texHalfRes_[1], 0);
        shaderP3_->setUniform("uAxis", 0.0f, 1.0f);
        shaderP3_->setUniform("uRadius", 3.0f);
        runPass(ctx, shaderP3_.get(), fboHalfRes_[0].get());
    } else {
        // Medium tier: skip the large bilateral; reuse mid-band as low-band.
        // P5 will see uMidBand == uLowBand which produces single-band smoothing.
        // Cheaper visual quality but still acceptable.
        //
        // We blit texHalfRes_[2] (mid-band) → texHalfRes_[0] (low-band slot)
        // so P5's binding setup below works uniformly.
        ctx->blitTextureToFramebuffer(*texHalfRes_[2], fboHalfRes_[0].get());
    }

    // ============== P4: upsample both bands ==============
    shaderP4_->use();
    shaderP4_->setUniform("uSourceTexelSize", halfTexelX, halfTexelY);

    // Mid-band: halfRes_[2] -> upsample_[0]
    shaderP4_->bindTexture("uSource", *texHalfRes_[2], 0);
    runPass(ctx, shaderP4_.get(), fboUpsample_[0].get());

    // Low-band: halfRes_[0] -> upsample_[1]
    shaderP4_->bindTexture("uSource", *texHalfRes_[0], 0);
    runPass(ctx, shaderP4_.get(), fboUpsample_[1].get());

    // ============== P5: multi-band Oklab composition ==============
    // Reads inputTex + upsample_[0] (mid) + upsample_[1] (low) + skinMask.
    // Writes to upsample_[0] (reuse — mid-band no longer needed).
    shaderP5_->use();
    shaderP5_->bindTexture("uOriginal",  inputTex,         0);
    shaderP5_->bindTexture("uMidBand",   *texUpsample_[0], 1);
    shaderP5_->bindTexture("uLowBand",   *texUpsample_[1], 2);
    shaderP5_->bindTexture("uSkinMask",  *texSkinMask_,    3);
    shaderP5_->setUniform("uSmoothingStrength", cfg_.smoothingStrength);
    shaderP5_->setUniform("uHighFreqStrength",  cfg_.highFreqStrength);
    shaderP5_->setUniform("uMidFreqStrength",   cfg_.midFreqStrength);
    shaderP5_->setUniform("uDetailPreserve",    cfg_.detailPreserve);
    shaderP5_->setUniform("uBlemishReduction",  cfg_.blemishReduction);
    shaderP5_->setUniform("uBaselineLuma",      baselineLuma);
    runPass(ctx, shaderP5_.get(), fboUpsample_[0].get());

    // ============== P5.5: specular control (optional per tier) ==============
    const TextureHandle* afterSpecular = texUpsample_[0].get();
    Framebuffer*         afterSpecFbo  = fboUpsample_[0].get();
    if (useSpecularPass) {
        shaderP55_->use();
        shaderP55_->bindTexture("uSource",   *texUpsample_[0], 0);
        shaderP55_->bindTexture("uSkinMask", *texSkinMask_,    1);
        shaderP55_->setUniform("uSpecularControl", currentSpecular_);
        shaderP55_->setUniform("uBaselineLuma",    baselineLuma);
        shaderP55_->setUniform("uTexelSize", fullTexelX, fullTexelY);
        runPass(ctx, shaderP55_.get(), fboUpsample_[1].get());
        afterSpecular = texUpsample_[1].get();
        afterSpecFbo  = fboUpsample_[1].get();
    }

    // ============== P6: glow finishing (warmth/lift/clarity) ==============
    // Read from afterSpecular, write to the OTHER upsample buffer.
    Framebuffer* p6Out = (afterSpecFbo == fboUpsample_[0].get())
                          ? fboUpsample_[1].get()
                          : fboUpsample_[0].get();
    const TextureHandle* p6OutTex = (afterSpecFbo == fboUpsample_[0].get())
                                     ? texUpsample_[1].get()
                                     : texUpsample_[0].get();
    shaderP6_->use();
    shaderP6_->bindTexture("uSource",   *afterSpecular, 0);
    shaderP6_->bindTexture("uSkinMask", *texSkinMask_,  1);
    shaderP6_->setUniform("uWarmth",         cfg_.warmth);
    shaderP6_->setUniform("uHighlightLift",  cfg_.highlightLift);
    shaderP6_->setUniform("uClarity",        cfg_.clarity);
    shaderP6_->setUniform("uBaselineLuma",   baselineLuma);
    shaderP6_->setUniform("uTexelSize", fullTexelX, fullTexelY);
    runPass(ctx, shaderP6_.get(), p6Out);

    // ============== P6.5: temporal stabilization ==============
    //
    // First-frame handling: seed history with current pre-temporal output
    // so the blend is effectively identity on frame 1. From frame 2 onward
    // we have a real previous frame to blend against.
    if (framesSinceActivation_ <= 1) {
        ctx->blitTextureToFramebuffer(*p6OutTex, fboPrevOutput_.get());
    }

    // Render P6.5 to a scratch buffer (the other of upsample[0/1] — the one
    // we used as input to P6 is now free). This avoids reading from a
    // texture that's bound as the current framebuffer, which is undefined
    // behavior in GLES.
    Framebuffer*         scratchFbo = afterSpecFbo;             // safe to reuse
    const TextureHandle* scratchTex = afterSpecular;            // matches scratchFbo

    shaderP65_->use();
    shaderP65_->bindTexture("uSource",   *p6OutTex,        0);
    shaderP65_->bindTexture("uPrevious", *texPrevOutput_,  1);
    shaderP65_->setUniform("uTemporalSmoothing", cfg_.temporalSmoothing);
    shaderP65_->setUniform("uMotionMagnitude",   motionMagnitude);
    runPass(ctx, shaderP65_.get(), scratchFbo);

    // Two blits using only existing render primitives:
    //   1. scratch → texPrevOutput_   (next frame's history input)
    //   2. scratch → outputFbo        (this frame's actual output)
    //
    // Cost: ~0.2 ms extra per frame for the second blit (the first one
    // would happen anyway when temporal blending is enabled). Acceptable;
    // avoids adding new render context API surface.
    //
    // The history holds the POST-temporal result so temporal smoothing
    // is properly recursive — this frame's stabilization decays into
    // next frame's blend.
    ctx->blitTextureToFramebuffer(*scratchTex, fboPrevOutput_.get());
    ctx->blitTextureToFramebuffer(*scratchTex, outputFbo);

    // ---- Frame-time measurement: feed benchmark or adaptive throttle ----
    auto frameEnd = std::chrono::steady_clock::now();
    float frameMs = std::chrono::duration<float, std::milli>(
                        frameEnd - frameStart).count();
    if (cfg_.quality == BeautyQuality::Auto) {
        if (!benchmarkComplete_) {
            recordFrameTime(frameMs);
        } else {
            adaptiveThrottle(frameMs);
        }
    }
}

// -----------------------------------------------------------------------------
// capturePerceptionState — populate frameState_ from a PerceptionFrame
//
// Strategy:
//   - "Dominant face" = face with the largest landmark AABB. FaceData has
//     no precomputed bounding box, so we compute it on the fly by walking
//     the landmark point array. ~468 points per face × <5 faces typical
//     = a few thousand float comparisons, ~0.01 ms on CPU. Cheaper than
//     plumbing a new field through the perception layer.
//   - If no faces visible: hasFaces=false → render() falls through to
//     passthrough. Beauty doesn't operate on empty scenes.
//   - visibleFaceCount feeds multi-face tier adjustment in prepare().
//
// The shader uniforms it feeds:
//   - uBaselineLuma — drives tone-aware threshold scaling (P3, P5, P5.5, P6)
//   - uMotionMagnitude — drives temporal blend gating + disocclusion guard
// -----------------------------------------------------------------------------
void SkinSmoothEffect::capturePerceptionState(const PerceptionFrame& frame) {
    frameState_ = FrameState{};  // reset
    frameState_.visibleFaceCount = static_cast<int>(frame.faces.size());

    if (frame.faces.empty()) {
        frameState_.hasFaces = false;
        return;
    }
    frameState_.hasFaces = true;

    // Find the dominant face by landmark-AABB area
    const FaceData* dominant = nullptr;
    float bestArea = 0.0f;
    for (const auto& face : frame.faces) {
        float minX = 1.0f, minY = 1.0f, maxX = 0.0f, maxY = 0.0f;
        for (const auto& p : face.landmarks.points) {
            if (p.x < minX) minX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.x > maxX) maxX = p.x;
            if (p.y > maxY) maxY = p.y;
        }
        float area = (maxX - minX) * (maxY - minY);
        if (area > bestArea) {
            bestArea = area;
            dominant = &face;
        }
    }
    if (!dominant) {
        frameState_.hasFaces = false;
        return;
    }

    // baselineLuma fallback: if skinTone is not yet valid (compute pass
    // still working through async readback), use a tone-neutral 0.5.
    // The first few frames after a face appears will use 0.5 until the
    // proper estimate lands ~5 frames later.
    if (dominant->skinTone.valid) {
        frameState_.baselineLuma = dominant->skinTone.baselineLuma;
    } else {
        frameState_.baselineLuma = 0.5f;
    }

    // motion is a flat float on FaceData — already face-size-normalized
    // per the perception_frame.h comment. Clamp to [0, 1] for shader use.
    frameState_.motionMagnitude = std::min(1.0f, std::max(0.0f,
                                            dominant->motion));
}

// -----------------------------------------------------------------------------
// recordFrameTime — feed the auto-tier benchmark with measured frame time
//
// First kBenchmarkFrames=10 frames at High tier are measured. On frame 11
// we lock the appropriate tier and set benchmarkComplete_=true so future
// frames go through adaptiveThrottle() instead.
//
// Tier selection thresholds (car-phase-3-requirements.md Decision 7):
//   avg < kHighTierMaxFrameMs   → High
//   avg < kMediumTierMaxFrameMs → Medium
//   else                        → Low
// -----------------------------------------------------------------------------
void SkinSmoothEffect::recordFrameTime(float frameMs) {
    benchmarkTotalMs_ += frameMs;
    benchmarkFrameCount_++;

    if (benchmarkFrameCount_ < kBenchmarkFrames) return;

    float avgMs = benchmarkTotalMs_ / (float)benchmarkFrameCount_;
    if (avgMs < kHighTierMaxFrameMs) {
        effectiveQuality_ = BeautyQuality::High;
    } else if (avgMs < kMediumTierMaxFrameMs) {
        effectiveQuality_ = BeautyQuality::Medium;
    } else {
        effectiveQuality_ = BeautyQuality::Low;
    }
    benchmarkComplete_ = true;
    // Reset throttle counters for the next phase
    overBudgetStreak_  = 0;
    underBudgetStreak_ = 0;
}

// -----------------------------------------------------------------------------
// adaptiveThrottle — runtime tier adjustment after benchmark completes
//
// Rule (car-phase-3-requirements.md, performance budget section):
//   - If frame time > kThrottleTriggerMs for kThrottleConsecutive frames
//     in a row → drop one tier
//   - If frame time stays under the current tier's threshold for
//     kRestoreHeadroomFrames consecutive frames → restore one tier
//
// The restore window is much longer than the drop window because:
//   - Dropping is a recovery from a real performance problem; should be quick
//   - Restoring risks oscillation if conditions are borderline
//   - 100 frames at 30fps ≈ 3.3 seconds of headroom proves stability
// -----------------------------------------------------------------------------
void SkinSmoothEffect::adaptiveThrottle(float frameMs) {
    if (frameMs > kThrottleTriggerMs) {
        overBudgetStreak_++;
        underBudgetStreak_ = 0;
        if (overBudgetStreak_ >= kThrottleConsecutive) {
            // Drop one tier (if not already at Low)
            if (effectiveQuality_ == BeautyQuality::High) {
                effectiveQuality_ = BeautyQuality::Medium;
                overBudgetStreak_ = 0;
            } else if (effectiveQuality_ == BeautyQuality::Medium) {
                effectiveQuality_ = BeautyQuality::Low;
                overBudgetStreak_ = 0;
            }
        }
        return;
    }

    // Frame was within budget. The threshold for "under budget" is the
    // *current tier's* threshold — Medium and Low have tighter expectations.
    float currentTierMax = kThrottleTriggerMs;
    if (effectiveQuality_ == BeautyQuality::Medium) {
        currentTierMax = kHighTierMaxFrameMs;
    } else if (effectiveQuality_ == BeautyQuality::Low) {
        currentTierMax = kMediumTierMaxFrameMs;
    }

    if (frameMs < currentTierMax) {
        underBudgetStreak_++;
        overBudgetStreak_ = 0;
        if (underBudgetStreak_ >= kRestoreHeadroomFrames) {
            // Promote one tier (if not already at High)
            if (effectiveQuality_ == BeautyQuality::Low) {
                effectiveQuality_ = BeautyQuality::Medium;
                underBudgetStreak_ = 0;
            } else if (effectiveQuality_ == BeautyQuality::Medium) {
                effectiveQuality_ = BeautyQuality::High;
                underBudgetStreak_ = 0;
            }
        }
    } else {
        // Frame was over budget but under throttle trigger — reset both
        overBudgetStreak_  = 0;
        underBudgetStreak_ = 0;
    }
}

void SkinSmoothEffect::renderPassthrough(const TextureHandle& inputTex,
                                          Framebuffer* outputFbo,
                                          RenderContext* ctx) {
    ctx->blitTextureToFramebuffer(inputTex, outputFbo);
}

}  // namespace community_ar
