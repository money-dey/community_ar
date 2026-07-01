// multiclass_segmenter_backend.cpp
// =============================================================================
// MulticlassSegmenterBackend implementation.
//
// Per-frame flow:
//   1. Run the multiclass model on the camera frame, producing a 6-channel
//      output texture (or, depending on the neural backend, a tensor that
//      can be bound as a 6-component texture).
//   2. Split that 6-channel output into 6 separate R8 textures using a
//      single MRT (multiple render targets) shader pass. This makes each
//      channel cheaply samplable by downstream effects.
//   3. Return all 6 textures in the SegmentationChannels struct.
//
// Step 2 is what makes the architectural promise from car-phase-3-requirements.md
// real: the model runs once, but downstream effects pay no per-channel
// inference cost.
//
// Performance notes:
//   - Model inference: ~8-10ms target on Snapdragon 7-class (car-phase-3 budget)
//   - Channel split: ~0.3ms (single full-screen MRT pass at 256x256)
//   - Total per frame: ~8-10ms, comparable to dedicated hair segmenter
// =============================================================================

#include "multiclass_segmenter_backend.h"
#include "../ml/neural_backend.h"
#include "../render/render_context.h"
#include <chrono>

namespace community_ar {

// -----------------------------------------------------------------------------
// Channel split shader
//
// Reads the 6-component output tensor and writes 6 separate R8 textures
// in a single fragment shader pass via gl_FragData[i] / location-bound
// outputs.
//
// GLSL ES 3.00 supports up to 8 color attachments on most devices; we use 6.
// MSL (Metal Shading Language) equivalent omitted here; produced from the
// same SPIRV at SPIRV-Cross unification time (Phase 5).
// -----------------------------------------------------------------------------
static const char* kSplitVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aUv;
}
)";

static const char* kSplitFS = R"GLSL(#version 300 es
precision mediump float;

// The 6-channel input. Layout depends on the neural backend:
//   - TFLite GPU delegate writes to a 6-component RGBA + 2-channel auxiliary
//     texture pair on some hardware. To stay portable we assume a single
//     6-component texture via sampler2DArray (one slice per channel).
//   - Core ML on iOS produces a CVPixelBuffer with planar layout.
// The NeuralBackend abstraction returns a uniform view here.
uniform sampler2DArray uTensor;

in  vec2 vUv;

// Six color attachments — one per output channel.
layout(location = 0) out vec4 outBackground;
layout(location = 1) out vec4 outHair;
layout(location = 2) out vec4 outBodySkin;
layout(location = 3) out vec4 outFaceSkin;
layout(location = 4) out vec4 outClothes;
layout(location = 5) out vec4 outOthers;

void main() {
    // Each channel is a softmax probability in [0, 1]. We write it to the
    // R component of a single-channel R8 texture; G/B/A are unused but
    // GLSL ES 3.00 doesn't let us declare 1-component out variables, so
    // we write 0 to the rest. The framebuffer's R8 attachment discards G/B/A.
    outBackground = vec4(texture(uTensor, vec3(vUv, 0.0)).r, 0.0, 0.0, 1.0);
    outHair       = vec4(texture(uTensor, vec3(vUv, 1.0)).r, 0.0, 0.0, 1.0);
    outBodySkin   = vec4(texture(uTensor, vec3(vUv, 2.0)).r, 0.0, 0.0, 1.0);
    outFaceSkin   = vec4(texture(uTensor, vec3(vUv, 3.0)).r, 0.0, 0.0, 1.0);
    outClothes    = vec4(texture(uTensor, vec3(vUv, 4.0)).r, 0.0, 0.0, 1.0);
    outOthers     = vec4(texture(uTensor, vec3(vUv, 5.0)).r, 0.0, 0.0, 1.0);
}
)GLSL";

// -----------------------------------------------------------------------------
// Constructor
//
// Lazy-loads the model on first run() to avoid blocking session creation
// behind disk I/O.
// -----------------------------------------------------------------------------
MulticlassSegmenterBackend::MulticlassSegmenterBackend(
        NeuralBackend* neuralBackend,
        const std::string& modelPath)
    : neuralBackend_(neuralBackend),
      modelPath_(modelPath) {}

// model_ (a unique_ptr<NeuralModel>) releases the loaded model via RAII.
MulticlassSegmenterBackend::~MulticlassSegmenterBackend() = default;

void MulticlassSegmenterBackend::ensureChannelTextures(RenderContext* ctx) {
    if (channelBackground_) return;

    // All channels are 256x256 — match the model's output resolution.
    auto makeChannel = [&]() {
        return std::shared_ptr<TextureHandle>(
            ctx->createTexture(256, 256, TextureHandle::Format::R8).release());
    };
    channelBackground_ = makeChannel();
    channelHair_       = makeChannel();
    channelBodySkin_   = makeChannel();
    channelFaceSkin_   = makeChannel();
    channelClothes_    = makeChannel();
    channelOthers_     = makeChannel();

    // The model's raw output is bound into this texture, then splitChannels()
    // fans it out to the six R8 channels above.
    tensorTex_ = std::shared_ptr<TextureHandle>(
        ctx->createTexture(256, 256, TextureHandle::Format::RGBA16F).release());

    // Shader and framebuffer set up once and reused.
    splitShader_ = ctx->createShader(kSplitVS, kSplitFS);

    // Framebuffer with 6 color attachments.
    // RenderContext exposes createMRTFramebuffer() — see render_context.h.
    std::vector<const TextureHandle*> attachments = {
        channelBackground_.get(), channelHair_.get(),
        channelBodySkin_.get(), channelFaceSkin_.get(),
        channelClothes_.get(), channelOthers_.get()
    };
    splitFbo_ = ctx->createMRTFramebuffer(attachments);
}

void MulticlassSegmenterBackend::splitChannels(const TextureHandle& tensorTex,
                                               RenderContext* ctx) {
    ctx->bindFramebuffer(splitFbo_.get());
    splitShader_->use();
    splitShader_->bindTexture("uTensor", tensorTex, 0);
    ctx->drawFullscreenQuad(splitShader_.get());
}

SegmentationChannels MulticlassSegmenterBackend::run(
        const TextureHandle& cameraTex,
        RenderContext* ctx) {

    SegmentationChannels channels;
    channels.backendName = name();

    // Lazy load on first invocation. loadModel() takes a model name/path and
    // returns a NeuralModel handle (owned here); nullptr means load failed.
    if (!model_) {
        model_ = neuralBackend_->loadModel(modelPath_);
        if (!model_) {
            channels.fresh = false;
            return channels;
        }
    }

    ensureChannelTextures(ctx);

    // Bind the camera frame as the model input. The backend handles rotation,
    // resize, and zero-copy-vs-blit internally (see Phase 1 diagnostics).
    CameraInputRect rect;
    rect.width  = cameraTex.width();
    rect.height = cameraTex.height();
    model_->setInputTexture(0, cameraTex, rect);

    auto t0 = std::chrono::steady_clock::now();
    bool ran = model_->run();
    auto t1 = std::chrono::steady_clock::now();
    lastInferenceMs_ = std::chrono::duration<float, std::milli>(t1 - t0).count();

    if (!ran) {
        channels.fresh = false;
        return channels;
    }

    // Bind the model's image output into our tensor texture, then split.
    if (!model_->bindOutputTexture(0, tensorTex_.get())) {
        channels.fresh = false;
        return channels;
    }
    splitChannels(*tensorTex_, ctx);

    // Populate output struct with shared references to our long-lived
    // channel textures. Downstream readers must not retain past the next
    // run() — we write into the same textures in place.
    channels.fresh = true;
    channels.background = channelBackground_;
    channels.hair       = channelHair_;
    channels.bodySkin   = channelBodySkin_;
    channels.faceSkin   = channelFaceSkin_;
    channels.clothes    = channelClothes_;
    channels.others     = channelOthers_;

    return channels;
}

}  // namespace community_ar
