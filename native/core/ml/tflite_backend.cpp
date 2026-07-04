// tflite_backend.cpp
// =============================================================================
// Production TensorFlow Lite backend.
//
// Two I/O modes, chosen at build time (see CMakeLists.txt):
//
//   1. CAR_TFLITE_GL_INTEROP (from-source TFLite via tools/fetch_tflite.sh +
//      Bazel): input/output tensors are bound directly to GL textures via the
//      GPU delegate's experimental Bind* API — fully zero-copy.
//
//   2. Default (prebuilt AARs via tools/fetch_tflite_prebuilt.sh): the
//      prebuilt binaries do NOT export the Bind* symbols (verified with
//      llvm-nm against 2.16.1), so tensor I/O is staged through CPU at the
//      boundaries: the model-input crop is blitted on GPU (crop+resize+
//      rotate in one pass), read back (≤256², ~0.1–0.3 MB), and copied into
//      the input tensor; image outputs (segmentation masks) are read from the
//      output tensor and uploaded into the caller's mask texture. Inference
//      itself is still GPU-accelerated by the V2 delegate.
//
// Invariant notes (CLAUDE.md):
//   - §1 "pixels never enter Dart" holds in both modes — staging happens
//     entirely inside this backend; masks re-enter GPU textures before any
//     effect samples them.
//   - §5 "GPU readback is asynchronous": the input readback here is
//     deliberately SYNCHRONOUS. Rationale: inference needs this frame's
//     pixels (a 1-frame-latent PBO scheme would add landmark latency), the
//     region is tiny (model input size, ≤256²), and the blit it waits on is
//     one quad draw. A double-buffered PBO variant is a future optimization
//     if profiling shows the stall matters.
//
// The camera texture arriving here is the WP-B ingress texture — already 2D
// (sampler2D), upright, and zoom-consistent. The old OES handling in this
// file predated the ingress pass and is gone.
// =============================================================================

#if defined(__ANDROID__)

#include "neural_backend.h"
#include "../render/render_context.h"
#include <android/log.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cstring>
#include <vector>

#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/delegates/gpu/delegate.h"
#if defined(CAR_TFLITE_GL_INTEROP)
// Only the from-source build ships these headers (and the matching symbols).
#include "tensorflow/lite/delegates/gpu/gl/egl_environment.h"
#include "tensorflow/lite/delegates/gpu/gl/gl_buffer.h"
#include "tensorflow/lite/delegates/gpu/gl/gl_texture.h"
#endif

#define LOG_TAG "CommunityAR-TFLite"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace community_ar {

namespace {

// -----------------------------------------------------------------------------
// CropResizeBlitter
//
// One GPU pass that samples a crop rect of the (2D, upright) camera ingress
// texture into a small RGBA framebuffer sized to the model input — crop,
// resize, rotation, and mirror (iris feeds mirrored right-eye crops) all in
// the same draw. readback() then stages those pixels to CPU for the tensor
// copy in the default (prebuilt) mode; in GL-interop mode the framebuffer's
// color texture is bound to the tensor directly.
// -----------------------------------------------------------------------------
class CropResizeBlitter {
public:
    explicit CropResizeBlitter(RenderContext* ctx) : ctx_(ctx) {}

    // Renders the crop into an internal framebuffer of exactly outW x outH.
    // Returns the color texture (valid until the next differently-sized call).
    const TextureHandle* blit(const TextureHandle& cameraTex,
                              const CameraInputRect& rect,
                              int outW, int outH) {
        if (!fbo_ || fbo_->width() != outW || fbo_->height() != outH) {
            fbo_ = ctx_->createFramebuffer(outW, outH,
                                           TextureHandle::Format::RGBA8);
            if (!fbo_) return nullptr;
        }
        ensureShader();
        if (!shader_) return nullptr;

        bakeTransform(rect, cameraTex.width(), cameraTex.height());

        ctx_->bindFramebuffer(fbo_.get());
        ctx_->clearColor(0, 0, 0, 1);
        shader_->use();
        shader_->bindTexture("uCamera", cameraTex, 0);
        shader_->setUniform("uTransform",
            transform_[0], transform_[1], transform_[2], transform_[3]);
        shader_->setUniform("uOffset", offset_[0], offset_[1]);
        ctx_->drawFullscreenQuad(shader_.get());
        return &fbo_->colorTexture();
    }

    // Reads the last blit back to CPU (RGBA8, tightly packed). SYNCHRONOUS by
    // design — see the invariant note in the file header. Must be called on
    // the render thread, right after blit().
    bool readback(std::vector<uint8_t>& outRgba, int* outW, int* outH) {
        if (!fbo_) return false;
        const int w = fbo_->width(), h = fbo_->height();
        outRgba.resize(static_cast<size_t>(w) * h * 4);
        ctx_->bindFramebuffer(fbo_.get());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, outRgba.data());
        if (outW) *outW = w;
        if (outH) *outH = h;
        return glGetError() == GL_NO_ERROR;
    }

    // Kept for the backend's per-frame hook; the blitter re-renders on every
    // blit() call (models need different crops/sizes anyway), so there is no
    // frame cache left to invalidate.
    void invalidate() {}

private:
    void ensureShader() {
        if (shader_) return;
        // Plain sampler2D — the input is the WP-B ingress texture, not OES.
        static const char* kVS = R"(#version 300 es
            precision highp float;
            layout(location = 0) in vec2 aPos;
            layout(location = 1) in vec2 aUv;
            uniform vec4 uTransform; // row-major 2x2: (a, b, c, d)
            uniform vec2 uOffset;
            out vec2 vUv;
            void main() {
                gl_Position = vec4(aPos, 0.0, 1.0);
                vUv = vec2(uTransform.x * aUv.x + uTransform.y * aUv.y,
                           uTransform.z * aUv.x + uTransform.w * aUv.y) + uOffset;
            }
        )";
        static const char* kFS = R"(#version 300 es
            precision mediump float;
            uniform sampler2D uCamera;
            in vec2 vUv;
            out vec4 fragColor;
            void main() { fragColor = texture(uCamera, vUv); }
        )";
        shader_ = ctx_->createShader(kVS, kFS);
    }

    // Compose (rotate+mirror about the crop centre) then (map into the crop
    // window in source-texture UV space):  uv_src = cropOff + cropScale *
    // (R*(uv - 0.5) + 0.5). Both steps are affine, so they fold into one
    // 2x2 + offset the vertex shader applies.
    void bakeTransform(const CameraInputRect& rect, int texW, int texH) {
        float c = 1.0f, s = 0.0f;
        switch (rect.rotationDeg) {
            case  90: c =  0; s =  1; break;
            case 180: c = -1; s =  0; break;
            case 270: c =  0; s = -1; break;
            default:  break;
        }
        float a = c, b = -s, cc = s, dd = c;
        if (rect.mirrorX) { a = -a; b = -b; }
        // Recentre the rotation around (0.5, 0.5) in crop-local UV space.
        const float ox = 0.5f - 0.5f * (a + b);
        const float oy = 0.5f - 0.5f * (cc + dd);

        // Crop window in source UV. A zero-sized rect means "whole texture".
        const float tw = std::max(texW, 1), th = std::max(texH, 1);
        const float sx = rect.width  > 0 ? rect.width  / tw : 1.0f;
        const float sy = rect.height > 0 ? rect.height / th : 1.0f;
        const float cx = rect.x / tw;
        const float cy = rect.y / th;

        transform_[0] = sx * a;  transform_[1] = sx * b;
        transform_[2] = sy * cc; transform_[3] = sy * dd;
        offset_[0] = cx + sx * ox;
        offset_[1] = cy + sy * oy;
    }

    RenderContext* ctx_;
    std::unique_ptr<Framebuffer>   fbo_;
    std::unique_ptr<ShaderProgram> shader_;
    float transform_[4] = {1, 0, 0, 1};
    float offset_[2]    = {0, 0};
};

// -----------------------------------------------------------------------------
// TfliteModel
// -----------------------------------------------------------------------------
class TfliteModel : public NeuralModel {
public:
    TfliteModel(TfLiteModel* model, TfLiteInterpreter* interp,
                TfLiteDelegate* gpuDelegate, bool gpuActive,
                RenderContext* renderCtx, CropResizeBlitter* blitter)
        : model_(model), interp_(interp),
          gpuDelegate_(gpuDelegate), gpuActive_(gpuActive),
          renderCtx_(renderCtx), blitter_(blitter) {
        introspect();
#if defined(CAR_TFLITE_GL_INTEROP)
        if (gpuActive_) bindGpuTensors();
#endif
    }

    ~TfliteModel() override {
        if (interp_) TfLiteInterpreterDelete(interp_);
        if (gpuDelegate_) TfLiteGpuDelegateV2Delete(gpuDelegate_);
        if (model_) TfLiteModelDelete(model_);
    }

    const std::vector<TensorSpec>& inputs() const override { return inputSpecs_; }
    const std::vector<TensorSpec>& outputs() const override { return outputSpecs_; }

    InputBindingMode setInputTexture(int inputIndex,
                                     const TextureHandle& tex,
                                     const CameraInputRect& srcRect) override {
        if (inputIndex < 0 ||
            inputIndex >= static_cast<int>(inputSpecs_.size())) {
            return InputBindingMode::CpuUpload;
        }

        // Model input size from the tensor shape [1, H, W, C], so the blit
        // performs the resize on GPU and the readback is as small as possible.
        const TensorShape& sh = inputSpecs_[inputIndex].shape;
        int mh = sh.rank >= 3 ? sh.dims[1] : 0;
        int mw = sh.rank >= 3 ? sh.dims[2] : 0;
        if (mw <= 0 || mh <= 0) {
            mw = std::max(srcRect.width, 16);
            mh = std::max(srcRect.height, 16);
        }

        const TextureHandle* small = blitter_->blit(tex, srcRect, mw, mh);
        if (!small) return InputBindingMode::CpuUpload;

#if defined(CAR_TFLITE_GL_INTEROP)
        if (gpuActive_) {
            GLuint glTexName = static_cast<GLuint>(small->nativeHandle());
            TfLiteStatus s = TfLiteGpuDelegateBindGlTextureToTensor(
                gpuDelegate_, glTexName, inputTensorIndex(inputIndex));
            if (s == kTfLiteOk) return InputBindingMode::GpuTextureZeroCopy;
            LOGW("BindGlTextureToTensor failed; staging through CPU instead");
        }
#endif
        // CPU-staged path (prebuilt TFLite): read the small blit back and copy
        // it into the input tensor. Normalization matches what a GL texture
        // binding would deliver: float [0, 1].
        if (!blitter_->readback(staging_, nullptr, nullptr)) {
            return InputBindingMode::CpuUpload;
        }
        copyStagingToInputTensor(inputIndex, mw, mh, srcRect.signedInput);
        return InputBindingMode::CpuUpload;
    }

    void setInputData(int inputIndex, const void* data, size_t bytes) override {
        TfLiteTensor* t = TfLiteInterpreterGetInputTensor(interp_, inputIndex);
        if (!t || TfLiteTensorByteSize(t) != bytes) return;
        TfLiteTensorCopyFromBuffer(t, data, bytes);
    }

    bool run() override {
        return TfLiteInterpreterInvoke(interp_) == kTfLiteOk;
    }

    bool readOutput(int outputIndex, void* dst, size_t bytes) override {
        const TfLiteTensor* t = TfLiteInterpreterGetOutputTensor(interp_, outputIndex);
        return t && TfLiteTensorCopyToBuffer(t, dst, bytes) == kTfLiteOk;
    }

    // Callers (segmenter backends) invoke this AFTER run() to land the mask
    // in a GPU texture. GL-interop mode binds the tensor storage; the
    // CPU-staged mode reads the output tensor and uploads it into the
    // caller's texture right here — same observable contract.
    bool bindOutputTexture(int outputIndex, TextureHandle* outTex) override {
        if (!outTex || !outTex->valid()) return false;
#if defined(CAR_TFLITE_GL_INTEROP)
        if (gpuActive_) {
            GLuint glName = static_cast<GLuint>(outTex->nativeHandle());
            TfLiteStatus s = TfLiteGpuDelegateBindGlTextureToTensor(
                gpuDelegate_, glName, outputTensorIndex(outputIndex));
            return s == kTfLiteOk;
        }
#endif
        return uploadOutputTensorToTexture(outputIndex, outTex);
    }

    int inputTensorIndex(int i) const { return inputIndices_[i]; }
    int outputTensorIndex(int i) const { return outputIndices_[i]; }

private:
    void introspect() {
        int nIn = TfLiteInterpreterGetInputTensorCount(interp_);
        for (int i = 0; i < nIn; ++i) {
            TfLiteTensor* t = TfLiteInterpreterGetInputTensor(interp_, i);
            inputSpecs_.push_back(specFromTensor(t));
            inputIndices_.push_back(i);
        }
        int nOut = TfLiteInterpreterGetOutputTensorCount(interp_);
        for (int i = 0; i < nOut; ++i) {
            const TfLiteTensor* t = TfLiteInterpreterGetOutputTensor(interp_, i);
            outputSpecs_.push_back(specFromTensor(t));
            outputIndices_.push_back(i);
        }
    }

#if defined(CAR_TFLITE_GL_INTEROP)
    void bindGpuTensors() {
        // Pre-declare tensor bindings at delegate-install time; some TFLite
        // versions require this before per-frame texture binds.
        for (int idx : inputIndices_) {
            TfLiteGpuDelegateBindGlBufferToTensor(gpuDelegate_, 0, idx);
        }
    }
#endif

    static TensorSpec specFromTensor(const TfLiteTensor* t) {
        TensorSpec s;
        s.name = TfLiteTensorName(t) ? TfLiteTensorName(t) : "";
        switch (TfLiteTensorType(t)) {
            case kTfLiteFloat32: s.type = TensorType::Float32; break;
            case kTfLiteUInt8:   s.type = TensorType::Uint8;   break;
            case kTfLiteInt32:   s.type = TensorType::Int32;   break;
            default:             s.type = TensorType::Float32; break;
        }
        int n = TfLiteTensorNumDims(t);
        s.shape.rank = n;
        for (int i = 0; i < n && i < 4; ++i) s.shape.dims[i] = TfLiteTensorDim(t, i);
        return s;
    }

    // RGBA8 staging → the input tensor's layout (HWC, C from the tensor
    // shape). Float tensors get [0, 1] by default, or [-1, 1] when the
    // caller flags a signed-input model (CameraInputRect::signedInput —
    // MediaPipe's face_detection is trained on [-1, 1]; FaceMesh/Iris/
    // segmenters on [0, 1]).
    void copyStagingToInputTensor(int inputIndex, int w, int h,
                                  bool signedInput) {
        TfLiteTensor* t = TfLiteInterpreterGetInputTensor(interp_, inputIndex);
        if (!t) return;
        const TensorShape& sh = inputSpecs_[inputIndex].shape;
        const int c = sh.rank >= 4 ? std::max(sh.dims[3], 1) : 3;
        const size_t n = static_cast<size_t>(w) * h;
        const uint8_t* src = staging_.data();

        if (TfLiteTensorType(t) == kTfLiteFloat32) {
            floatStaging_.resize(n * c);
            float* dst = floatStaging_.data();
            const float scale  = signedInput ? 2.0f / 255.0f : 1.0f / 255.0f;
            const float offset = signedInput ? -1.0f : 0.0f;
            for (size_t i = 0; i < n; ++i) {
                for (int ch = 0; ch < c; ++ch) {
                    dst[i * c + ch] =
                        (ch < 4 ? src[i * 4 + ch] : 0) * scale + offset;
                }
            }
            const size_t bytes = floatStaging_.size() * sizeof(float);
            if (TfLiteTensorByteSize(t) == bytes) {
                TfLiteTensorCopyFromBuffer(t, floatStaging_.data(), bytes);
            } else {
                LOGW("input tensor size mismatch: have %zu want %zu",
                     bytes, TfLiteTensorByteSize(t));
            }
        } else if (TfLiteTensorType(t) == kTfLiteUInt8) {
            byteStaging_.resize(n * c);
            for (size_t i = 0; i < n; ++i) {
                for (int ch = 0; ch < c; ++ch) {
                    byteStaging_[i * c + ch] = ch < 4 ? src[i * 4 + ch] : 0;
                }
            }
            if (TfLiteTensorByteSize(t) == byteStaging_.size()) {
                TfLiteTensorCopyFromBuffer(t, byteStaging_.data(),
                                           byteStaging_.size());
            }
        }
    }

    // Output tensor [1, H, W, C] → the caller's texture (R8 masks take
    // channel 0; RGBA takes up to 4 channels, alpha defaulting to opaque).
    // Values are clamped from the tensor's native range assuming [0, 1]
    // mask semantics — the same assumption the GL binding path made.
    bool uploadOutputTensorToTexture(int outputIndex, TextureHandle* tex) {
        const TfLiteTensor* t =
            TfLiteInterpreterGetOutputTensor(interp_, outputIndex);
        if (!t) return false;
        const int rank = TfLiteTensorNumDims(t);
        if (rank < 3) return false;
        const int th = TfLiteTensorDim(t, 1);
        const int tw = TfLiteTensorDim(t, 2);
        const int tc = rank >= 4 ? std::max(TfLiteTensorDim(t, 3), 1) : 1;
        if (tex->width() != tw || tex->height() != th) {
            // Size mismatch — the caller allocated its mask texture at a
            // different resolution than the model output. Bail (caller treats
            // this as "mask not fresh") rather than silently resampling.
            LOGW("output %d: tensor %dx%d vs texture %dx%d — skipping upload",
                 outputIndex, tw, th, tex->width(), tex->height());
            return false;
        }

        const bool isR8 = tex->format() == TextureHandle::Format::R8;
        const int dstC = isR8 ? 1 : 4;
        const size_t n = static_cast<size_t>(tw) * th;
        uploadStaging_.resize(n * dstC);

        if (TfLiteTensorType(t) == kTfLiteFloat32) {
            const float* src = static_cast<const float*>(
                TfLiteTensorData(const_cast<TfLiteTensor*>(t)));
            if (!src) return false;
            for (size_t i = 0; i < n; ++i) {
                for (int ch = 0; ch < dstC; ++ch) {
                    float v = ch < tc ? src[i * tc + ch] : (ch == 3 ? 1.f : 0.f);
                    v = std::min(1.f, std::max(0.f, v));
                    uploadStaging_[i * dstC + ch] =
                        static_cast<uint8_t>(v * 255.0f + 0.5f);
                }
            }
        } else if (TfLiteTensorType(t) == kTfLiteUInt8) {
            const uint8_t* src = static_cast<const uint8_t*>(
                TfLiteTensorData(const_cast<TfLiteTensor*>(t)));
            if (!src) return false;
            for (size_t i = 0; i < n; ++i) {
                for (int ch = 0; ch < dstC; ++ch) {
                    uploadStaging_[i * dstC + ch] =
                        ch < tc ? src[i * tc + ch] : (ch == 3 ? 255 : 0);
                }
            }
        } else {
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex->nativeHandle()));
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, th,
                        isR8 ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE,
                        uploadStaging_.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        return glGetError() == GL_NO_ERROR;
    }

    TfLiteModel*       model_       = nullptr;
    TfLiteInterpreter* interp_      = nullptr;
    TfLiteDelegate*    gpuDelegate_ = nullptr;
    bool               gpuActive_   = false;
    RenderContext*     renderCtx_   = nullptr;
    CropResizeBlitter* blitter_     = nullptr;
    std::vector<TensorSpec> inputSpecs_, outputSpecs_;
    std::vector<int> inputIndices_, outputIndices_;
    std::vector<uint8_t> staging_;        // RGBA8 readback of the input blit
    std::vector<float>   floatStaging_;   // converted input tensor data
    std::vector<uint8_t> byteStaging_;    // uint8 input tensor data
    std::vector<uint8_t> uploadStaging_;  // output tensor → texture bytes
};

}  // anonymous namespace

// -----------------------------------------------------------------------------
// TfliteBackend
// -----------------------------------------------------------------------------
class TfliteBackend : public NeuralBackend {
public:
    explicit TfliteBackend(const BackendConfig& cfg)
        : cfg_(cfg), blitter_(cfg.renderContext) {}

    std::unique_ptr<NeuralModel> loadModel(const std::string& name) override {
        std::string path = cfg_.modelDirectory + "/" + name + ".tflite";
        TfLiteModel* m = TfLiteModelCreateFromFile(path.c_str());
        if (!m) { LOGE("Failed to load %s", path.c_str()); return nullptr; }

        TfLiteInterpreterOptions* opts = TfLiteInterpreterOptionsCreate();
        TfLiteInterpreterOptionsSetNumThreads(opts, cfg_.numCpuThreads);

        TfLiteDelegate* gpuDel = nullptr;
        bool gpuActive = false;
        if (cfg_.preferGpu) {
            TfLiteGpuDelegateOptionsV2 gpuOpts = TfLiteGpuDelegateOptionsV2Default();
            gpuOpts.inference_priority1 = TFLITE_GPU_INFERENCE_PRIORITY_MIN_LATENCY;
            gpuOpts.inference_priority2 = TFLITE_GPU_INFERENCE_PRIORITY_MIN_MEMORY_USAGE;
            gpuOpts.experimental_flags |=
                TFLITE_GPU_EXPERIMENTAL_FLAGS_ENABLE_QUANT;
            gpuDel = TfLiteGpuDelegateV2Create(&gpuOpts);
            if (gpuDel) {
                TfLiteInterpreterOptionsAddDelegate(opts, gpuDel);
                gpuActive = true;
#if defined(CAR_TFLITE_GL_INTEROP)
                activeAccelerator_ = "GPU (zero-copy)";
#else
                activeAccelerator_ = "GPU (CPU-staged I/O)";
#endif
            }
        }

        TfLiteInterpreter* interp = TfLiteInterpreterCreate(m, opts);
        TfLiteInterpreterOptionsDelete(opts);
        if (!interp) {
            if (gpuDel) TfLiteGpuDelegateV2Delete(gpuDel);
            TfLiteModelDelete(m);
            return nullptr;
        }
        if (TfLiteInterpreterAllocateTensors(interp) != kTfLiteOk) {
            TfLiteInterpreterDelete(interp);
            if (gpuDel) TfLiteGpuDelegateV2Delete(gpuDel);
            TfLiteModelDelete(m);
            return nullptr;
        }

        auto model = std::make_unique<TfliteModel>(m, interp, gpuDel, gpuActive,
                                                   cfg_.renderContext, &blitter_);
        // One-line tensor-shape dump: on-device this is what verifies layout
        // assumptions (e.g. BlazeFace's two-output [1,N,16]+[1,N,1] shape).
        std::string shapes;
        char buf[48];
        for (const auto& s : model->inputs()) {
            snprintf(buf, sizeof(buf), " in[%d,%d,%d,%d]", s.shape.dims[0],
                     s.shape.dims[1], s.shape.dims[2], s.shape.dims[3]);
            shapes += buf;
        }
        for (const auto& s : model->outputs()) {
            snprintf(buf, sizeof(buf), " out[%d,%d,%d,%d]", s.shape.dims[0],
                     s.shape.dims[1], s.shape.dims[2], s.shape.dims[3]);
            shapes += buf;
        }
        LOGI("Loaded %s (accelerator: %s)%s", name.c_str(),
             activeAccelerator_.c_str(), shapes.c_str());
        return model;
    }

    const char* activeAcceleratorName() const override {
        return activeAccelerator_.c_str();
    }

    InputBindingMode preferredBindingMode(NeuralModel* /*m*/) const override {
#if defined(CAR_TFLITE_GL_INTEROP)
        if (activeAccelerator_.find("GPU") != std::string::npos) {
            return InputBindingMode::GpuTextureZeroCopy;
        }
#endif
        return InputBindingMode::CpuUpload;
    }

    // Per-frame hook kept for the perception pipeline; no cached state left.
    void invalidateFrame() { blitter_.invalidate(); }

private:
    BackendConfig cfg_;
    CropResizeBlitter blitter_;
    std::string activeAccelerator_ = "CPU";
};

std::unique_ptr<NeuralBackend> createTfliteBackend(const BackendConfig& cfg) {
    if (!cfg.renderContext) {
        LOGE("TfliteBackend requires renderContext in BackendConfig");
        return nullptr;
    }
    return std::make_unique<TfliteBackend>(cfg);
}

// Per-frame hook called by the perception pipeline (extern "C" declaration in
// perception_pipeline.cpp; the stub backend defines the same symbol as a
// no-op). The downcast is safe: on Android the backend is always the
// TfliteBackend created above.
extern "C" void tflite_backend_invalidate_frame(NeuralBackend* b) {
    if (b) static_cast<TfliteBackend*>(b)->invalidateFrame();
}

}  // namespace community_ar

#endif  // __ANDROID__
