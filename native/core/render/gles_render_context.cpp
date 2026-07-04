// gles_render_context.cpp
// =============================================================================
// OpenGL ES 3.0 implementation of RenderContext. Used on Android.
//
// Key Android-specific details:
//   - Camera textures arrive as OES external textures (samplerExternalOES),
//     not regular sampler2D. They need the GL_OES_EGL_image_external_essl3
//     extension and a different sampler type in shaders.
//   - For Phase 0 we sample camera textures via a tiny intermediate "blit
//     pass" that copies OES → sampler2D, so downstream shaders can be
//     written normally. Optimal production code would either author all
//     shaders with sampler types templated, or do the copy at frame ingress
//     once. For Phase 0 we just use the blit approach for simplicity.
// =============================================================================

#if defined(__ANDROID__)

#include "render_context.h"
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <android/log.h>
#include <vector>

#define LOG_TAG "CommunityAR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace community_ar {

// -----------------------------------------------------------------------------
// TextureHandle implementation
// -----------------------------------------------------------------------------
TextureHandle::TextureHandle() = default;

TextureHandle::TextureHandle(uint64_t handle, int w, int h, Format f, Ownership o)
    : nativeHandle_(handle), width_(w), height_(h), format_(f), ownership_(o) {}

TextureHandle::~TextureHandle() {
    if (ownership_ == Ownership::Owned && nativeHandle_ != 0) {
        GLuint t = static_cast<GLuint>(nativeHandle_);
        glDeleteTextures(1, &t);
    }
}

TextureHandle::TextureHandle(TextureHandle&& o) noexcept
    : nativeHandle_(o.nativeHandle_), width_(o.width_), height_(o.height_),
      format_(o.format_), ownership_(o.ownership_) {
    o.nativeHandle_ = 0;
}

TextureHandle& TextureHandle::operator=(TextureHandle&& o) noexcept {
    if (this != &o) {
        if (ownership_ == Ownership::Owned && nativeHandle_ != 0) {
            GLuint t = static_cast<GLuint>(nativeHandle_);
            glDeleteTextures(1, &t);
        }
        nativeHandle_ = o.nativeHandle_;
        width_ = o.width_; height_ = o.height_;
        format_ = o.format_; ownership_ = o.ownership_;
        o.nativeHandle_ = 0;
    }
    return *this;
}

// -----------------------------------------------------------------------------
// GLES Framebuffer
// -----------------------------------------------------------------------------
class GlesFramebuffer : public Framebuffer {
public:
    GlesFramebuffer(int w, int h, TextureHandle::Format format) : w_(w), h_(h) {
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        GLenum internalFormat = (format == TextureHandle::Format::RGBA16F)
                                ? GL_RGBA16F : GL_RGBA8;
        GLenum type = (format == TextureHandle::Format::RGBA16F)
                      ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, GL_RGBA, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            LOGE("Framebuffer incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        colorTexture_ = TextureHandle(tex, w, h, format,
                                      TextureHandle::Ownership::Owned);
    }

    // Adopt an already-created framebuffer object — e.g. an MRT framebuffer
    // whose color attachments are owned externally (see createMRTFramebuffer).
    // Takes ownership of `existingFbo` (deleted in the destructor); no single
    // color texture is tracked (colorTexture() returns an invalid handle).
    GlesFramebuffer(GLuint existingFbo, int w, int h)
        : w_(w), h_(h), fbo_(existingFbo) {}

    ~GlesFramebuffer() override {
        if (fbo_) glDeleteFramebuffers(1, &fbo_);
    }

    int width() const override { return w_; }
    int height() const override { return h_; }
    const TextureHandle& colorTexture() const override { return colorTexture_; }
    uint64_t nativeHandle() const override { return fbo_; }

private:
    int w_, h_;
    GLuint fbo_ = 0;
    TextureHandle colorTexture_;
};

// -----------------------------------------------------------------------------
// GLES ShaderProgram
// -----------------------------------------------------------------------------
class GlesShaderProgram : public ShaderProgram {
public:
    GlesShaderProgram(const std::string& vs, const std::string& fs) {
        GLuint v = compile(GL_VERTEX_SHADER, vs);
        GLuint f = compile(GL_FRAGMENT_SHADER, fs);
        program_ = glCreateProgram();
        glAttachShader(program_, v);
        glAttachShader(program_, f);
        glLinkProgram(program_);
        GLint linked;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        linked_ = linked != 0;
        if (!linked_) {
            char log[1024];
            glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
            LOGE("Shader link failed: %s", log);
        }
        glDeleteShader(v);
        glDeleteShader(f);
    }

    ~GlesShaderProgram() override {
        if (program_) glDeleteProgram(program_);
    }

    // Using an unlinked program raises GL_INVALID_OPERATION on every draw —
    // on-device that surfaced as per-frame 0x502 spam that drowned out other
    // failures. Log once and leave the previous program bound instead; the
    // pass renders wrong but the error state stays clean and diagnosable.
    void use() override {
        if (!linked_) {
            if (!warnedUnlinked_) {
                warnedUnlinked_ = true;
                LOGE("use() on unlinked shader program %u — pass disabled "
                     "(see the link failure above)", program_);
            }
            return;
        }
        glUseProgram(program_);
    }

    void setUniform(const char* n, float v) override {
        glUniform1f(loc(n), v);
    }
    void setUniform(const char* n, float x, float y) override {
        glUniform2f(loc(n), x, y);
    }
    void setUniform(const char* n, float x, float y, float z) override {
        glUniform3f(loc(n), x, y, z);
    }
    void setUniform(const char* n, float x, float y, float z, float w) override {
        glUniform4f(loc(n), x, y, z, w);
    }
    void setUniform(const char* n, int v) override {
        glUniform1i(loc(n), v);
    }
    void setUniformMatrix4(const char* n, const float* v) override {
        glUniformMatrix4fv(loc(n), 1, GL_FALSE, v);
    }

    void bindTexture(const char* samplerName, const TextureHandle& tex, int unit) override {
        glActiveTexture(GL_TEXTURE0 + unit);
        GLenum target = (tex.format() == TextureHandle::Format::ExternalOES)
                        ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;
        glBindTexture(target, static_cast<GLuint>(tex.nativeHandle()));
        glUniform1i(loc(samplerName), unit);
    }

private:
    GLuint program_ = 0;
    bool   linked_ = false;
    bool   warnedUnlinked_ = false;

    GLint loc(const char* name) {
        return glGetUniformLocation(program_, name);
    }

    static GLuint compile(GLenum type, const std::string& src) {
        GLuint s = glCreateShader(type);
        const char* p = src.c_str();
        glShaderSource(s, 1, &p, nullptr);
        glCompileShader(s);
        GLint compiled;
        glGetShaderiv(s, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            char log[1024];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            LOGE("Shader compile failed: %s\nSource:\n%s", log, src.c_str());
        }
        return s;
    }
};

// -----------------------------------------------------------------------------
// GLES instanced shader — a shader program that remembers its vertex format.
//
// createInstancedShader() gets the InstancedVertexFormat but the base
// GlesShaderProgram had nowhere to keep it, so per-instance layouts were
// dropped and drawInstancedQuads stayed a silent no-op (the debug overlay's
// dots never drew — same stub landmine class as blit()/the rasterizer VBOs).
// The format lives here; drawInstancedQuads applies it at draw time.
// -----------------------------------------------------------------------------
class GlesInstancedShaderProgram : public GlesShaderProgram {
public:
    GlesInstancedShaderProgram(const std::string& vs, const std::string& fs,
                               InstancedVertexFormat fmt)
        : GlesShaderProgram(vs, fs), fmt_(std::move(fmt)) {}
    const InstancedVertexFormat& format() const { return fmt_; }
private:
    InstancedVertexFormat fmt_;
};

// -----------------------------------------------------------------------------
// GLES VertexBuffer — RAII VBO wrapper (extended API; the default-nullptr
// stubs crashed the mask rasterizer the first time a face was detected).
// -----------------------------------------------------------------------------
class GlesVertexBuffer : public VertexBuffer {
public:
    GlesVertexBuffer(const void* data, size_t bytes, GLenum usage)
        : size_(bytes), usage_(usage) {
        glGenBuffers(1, &vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, data, usage);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    ~GlesVertexBuffer() override { if (vbo_) glDeleteBuffers(1, &vbo_); }

    uint64_t nativeHandle() const override { return vbo_; }
    size_t   sizeBytes() const override { return size_; }

    // Re-fill (used by uploadDynamicVertexBuffer); grows the store if needed.
    void upload(const void* data, size_t bytes) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        if (bytes > size_) {
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, data, usage_);
            size_ = bytes;
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, data);
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

private:
    GLuint vbo_ = 0;
    size_t size_ = 0;
    GLenum usage_;
};

// -----------------------------------------------------------------------------
// GLES RenderContext
// -----------------------------------------------------------------------------
class GlesRenderContext : public RenderContext {
public:
    GlesRenderContext(EGLContext eglCtx, EGLDisplay eglDpy)
        : eglContext_(eglCtx), eglDisplay_(eglDpy) {
        // Set up a single VBO + VAO for the fullscreen quad.
        // GLES 3.0 supports VAOs natively.
        static const float kQuad[] = {
            // pos    // uv
            -1.f,-1.f,  0.f, 0.f,
             1.f,-1.f,  1.f, 0.f,
            -1.f, 1.f,  0.f, 1.f,
             1.f, 1.f,  1.f, 1.f,
        };
        glGenVertexArrays(1, &quadVao_);
        glBindVertexArray(quadVao_);
        glGenBuffers(1, &quadVbo_);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),
                              reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),
                              reinterpret_cast<void*>(2*sizeof(float)));
        glBindVertexArray(0);

        GLint maxTexSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
        maxTextureSize_ = maxTexSize;

        LOGI("GLES RenderContext initialized. GL_VERSION: %s",
             glGetString(GL_VERSION));
    }

    ~GlesRenderContext() override {
        if (quadVbo_) glDeleteBuffers(1, &quadVbo_);
        if (quadVao_) glDeleteVertexArrays(1, &quadVao_);
    }

    std::unique_ptr<TextureHandle> createTexture(int w, int h,
                                                 TextureHandle::Format format) override {
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        GLenum internalFmt = (format == TextureHandle::Format::R8) ? GL_R8 : GL_RGBA8;
        GLenum dataFmt = (format == TextureHandle::Format::R8) ? GL_RED : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, dataFmt,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        return std::make_unique<TextureHandle>(
            tex, w, h, format, TextureHandle::Ownership::Owned);
    }

    std::unique_ptr<Framebuffer> createFramebuffer(int w, int h,
                                                   TextureHandle::Format format) override {
        return std::make_unique<GlesFramebuffer>(w, h, format);
    }

    std::unique_ptr<ShaderProgram> createShader(const std::string& vs,
                                                const std::string& fs) override {
        return std::make_unique<GlesShaderProgram>(vs, fs);
    }

    // Wrap an EXISTING texture in a framebuffer so it can be rendered into.
    // Load-bearing for the effect chain (ping-pong FBOs), the mask rasterizer,
    // and the beauty pipeline's intermediate targets — all of which allocate
    // their own textures and render into them. This was the "pre-existing gap"
    // flagged in render_context.h at consolidation time: every caller received
    // the default-nullptr and the effect chain could never have rendered.
    // The returned framebuffer owns only the FBO object; the texture stays
    // caller-owned (the adopt constructor's colorTexture() is intentionally
    // invalid — callers sample their own texture handle).
    std::unique_ptr<Framebuffer> createFramebufferForTexture(
            TextureHandle& tex) override {
        if (!tex.valid()) return nullptr;
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,
                               static_cast<GLuint>(tex.nativeHandle()), 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            LOGE("createFramebufferForTexture: incomplete (0x%x)", status);
            glDeleteFramebuffers(1, &fbo);
            return nullptr;
        }
        return std::make_unique<GlesFramebuffer>(fbo, tex.width(), tex.height());
    }

    void bindFramebuffer(Framebuffer* fbo) override {
        if (fbo) {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo->nativeHandle()));
            glViewport(0, 0, fbo->width(), fbo->height());
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    // ---- Extended API: vertex buffers + rasterizer shader + blending ----
    // These were default-nullptr stubs; the mask rasterizer null-dereffed the
    // first time a face was detected on-device (mask_rasterizer.cpp:197).

    std::unique_ptr<VertexBuffer> createVertexBuffer(
            const void* data, size_t bytes) override {
        return std::make_unique<GlesVertexBuffer>(data, bytes, GL_STATIC_DRAW);
    }

    std::unique_ptr<VertexBuffer> createDynamicVertexBuffer(
            size_t maxBytes) override {
        return std::make_unique<GlesVertexBuffer>(nullptr, maxBytes,
                                                  GL_DYNAMIC_DRAW);
    }

    void uploadDynamicVertexBuffer(VertexBuffer* vbo, const void* data,
                                   size_t bytes) override {
        if (vbo && data && bytes) {
            static_cast<GlesVertexBuffer*>(vbo)->upload(data, bytes);
        }
    }

    // The returned program carries its vertex format so drawInstancedQuads can
    // apply it at draw time. Callers that never instance (the mask rasterizer
    // passes perInstanceStride = 0 and draws via drawTriangles, which sets its
    // own pos-2f + alpha-1f layout) are unaffected.
    std::unique_ptr<ShaderProgram> createInstancedShader(
            const std::string& vs, const std::string& fs,
            const InstancedVertexFormat& fmt) override {
        return std::make_unique<GlesInstancedShaderProgram>(vs, fs, fmt);
    }

    // Instanced quads (debug-overlay dots): per-vertex corners + per-instance
    // center/radius/color via glVertexAttribDivisor — core in ES 3.0.
    //
    // Blend state is owned HERE, and it is classic src-alpha: the overlay
    // composites over the effect output. Deliberately NOT
    // enableAlphaBlending(), which is ADDITIVE and belongs to the mask
    // rasterizer's accumulate-and-clamp scheme (see the comment there) — the
    // two consumers need different blending, so each draw path sets its own.
    //
    // `program` must come from createInstancedShader with a non-empty
    // per-instance layout (the only way to express an instanced draw in this
    // API); anything else is a caller bug we degrade on, log-once.
    void drawInstancedQuads(ShaderProgram* program,
                            VertexBuffer* perVertex, int vertexCount,
                            VertexBuffer* perInstance,
                            int instanceCount) override {
        if (!program || !perVertex || !perInstance ||
            vertexCount <= 0 || instanceCount <= 0) return;
        const auto& fmt =
            static_cast<GlesInstancedShaderProgram*>(program)->format();
        if (fmt.perInstanceStride <= 0 || fmt.perInstanceAttrs.empty()) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                LOGE("drawInstancedQuads: shader has no per-instance layout");
            }
            return;
        }

        glBindBuffer(GL_ARRAY_BUFFER,
                     static_cast<GLuint>(perVertex->nativeHandle()));
        for (const auto& a : fmt.perVertexAttrs) {
            glEnableVertexAttribArray(a.location);
            glVertexAttribPointer(a.location, a.size, GL_FLOAT, GL_FALSE,
                                  fmt.perVertexStride,
                                  reinterpret_cast<void*>((intptr_t)a.offset));
            glVertexAttribDivisor(a.location, 0);
        }
        glBindBuffer(GL_ARRAY_BUFFER,
                     static_cast<GLuint>(perInstance->nativeHandle()));
        for (const auto& a : fmt.perInstanceAttrs) {
            glEnableVertexAttribArray(a.location);
            glVertexAttribPointer(a.location, a.size, GL_FLOAT, GL_FALSE,
                                  fmt.perInstanceStride,
                                  reinterpret_cast<void*>((intptr_t)a.offset));
            glVertexAttribDivisor(a.location, 1);
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArraysInstanced(GL_TRIANGLES, 0, vertexCount, instanceCount);
        glDisable(GL_BLEND);

        // Reset divisors so later non-instanced draws (drawTriangles reuses
        // these attrib slots) don't inherit instancing.
        for (const auto& a : fmt.perVertexAttrs)
            glDisableVertexAttribArray(a.location);
        for (const auto& a : fmt.perInstanceAttrs) {
            glVertexAttribDivisor(a.location, 0);
            glDisableVertexAttribArray(a.location);
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // Size of the render target bindFramebuffer/bindDisplayFramebuffer set up
    // (both set the viewport to the full target, so the viewport IS the size).
    // The debug overlay divides by this for circular dots — the default no-op
    // would hand it 0×0 and NaN out every dot position.
    void currentFramebufferSize(int* outW, int* outH) const override {
        GLint vp[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, vp);
        if (outW) *outW = vp[2];
        if (outH) *outH = vp[3];
    }

    // ADDITIVE blending, deliberately: the only consumer is the mask
    // rasterizer, whose overlapping fan triangles are expected to accumulate
    // ("internally hot" mask clamped by smoothstep — see the concave-contour
    // gotcha in CLAUDE.md). Classic src-alpha blending would break that.
    void enableAlphaBlending(bool enable) override {
        if (enable) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
        } else {
            glDisable(GL_BLEND);
        }
    }

    void bindDisplayFramebuffer(int width, int height) override {
        // fbo 0 is the EGL window surface (Option A: owned by the Kotlin layer).
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
    }

    void clearColor(float r, float g, float b, float a) override {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void drawFullscreenQuad(ShaderProgram*) override {
        glBindVertexArray(quadVao_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }

    // Shader-based texture→framebuffer copy. This was a stub ("implemented at
    // the Session layer") — but it is load-bearing in six call sites: the
    // effect graph's empty-graph camera copy, SkinSmooth's mid-band
    // preservation, temporal-history seed/update, its FINAL output write, and
    // its no-face passthrough. On-device the stub meant the beauty pipeline
    // computed everything and then never delivered a pixel → black viewport.
    void blit(const TextureHandle& src, Framebuffer* dst) override {
        if (!dst || !src.valid()) return;
        if (!blitShader_) {
            static const char* kBlitVS = R"(#version 300 es
                precision highp float;
                layout(location = 0) in vec2 aPos;
                layout(location = 1) in vec2 aUv;
                out vec2 vUv;
                void main() { gl_Position = vec4(aPos, 0.0, 1.0); vUv = aUv; }
            )";
            static const char* kBlitFS = R"(#version 300 es
                precision mediump float;
                uniform sampler2D uTex;
                in vec2 vUv;
                out vec4 fragColor;
                void main() { fragColor = texture(uTex, vUv); }
            )";
            blitShader_ = std::make_unique<GlesShaderProgram>(kBlitVS, kBlitFS);
        }
        bindFramebuffer(dst);
        blitShader_->use();
        blitShader_->bindTexture("uTex", src, 0);
        drawFullscreenQuad(blitShader_.get());
    }

    void flush() override { glFlush(); }
    void waitGpu() override { glFinish(); }

    const char* backendName() const override { return "OpenGL ES 3.0"; }
    int maxTextureSize() const override { return maxTextureSize_; }

    // -------------------------------------------------------------------------
    // Extended API (merged from gles_render_context_phase3_updates.cpp).
    // These were defined out-of-line in a separate TU that couldn't see this
    // class; consolidated here so they compile.
    // -------------------------------------------------------------------------

    // Draw an arbitrary triangle list. Vertex layout matches the mask
    // rasterizer's: position (loc 0, 2 floats) + alpha (loc 1, 1 float),
    // interleaved.
    void drawTriangles(ShaderProgram* shader, VertexBuffer* vbo,
                       int firstVertex, int vertexCount) override {
        if (!shader || !vbo || vertexCount <= 0) return;

        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(vbo->nativeHandle()));

        constexpr GLsizei kStride = sizeof(float) * 3;
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, kStride, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, kStride,
                              (void*)(sizeof(float) * 2));

        glDrawArrays(GL_TRIANGLES, firstVertex, vertexCount);

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
    }

    // Create a framebuffer with N color attachments (MRT). Used by the
    // multiclass segmenter to split one tensor into 6 R8 textures in one pass.
    // The attached textures are owned by the caller; the returned framebuffer
    // owns only the FBO object.
    std::unique_ptr<Framebuffer> createMRTFramebuffer(
            const std::vector<const TextureHandle*>& colorAttachments) override {
        if (colorAttachments.empty()) {
            LOGE("createMRTFramebuffer: empty attachments list");
            return nullptr;
        }

        static GLint sMaxAttachments = 0;
        if (sMaxAttachments == 0) {
            glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &sMaxAttachments);
        }
        if ((GLint)colorAttachments.size() > sMaxAttachments) {
            LOGE("createMRTFramebuffer: %zu attachments requested, max is %d",
                 colorAttachments.size(), sMaxAttachments);
            return nullptr;
        }

        // All attachments must be the same size.
        int width = 0, height = 0;
        for (size_t i = 0; i < colorAttachments.size(); ++i) {
            const TextureHandle* tex = colorAttachments[i];
            if (!tex) {
                LOGE("createMRTFramebuffer: null attachment at index %zu", i);
                return nullptr;
            }
            if (i == 0) {
                width = tex->width();
                height = tex->height();
            } else if (tex->width() != width || tex->height() != height) {
                LOGE("createMRTFramebuffer: attachment %zu size mismatch", i);
                return nullptr;
            }
        }

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        std::vector<GLenum> drawBuffers(colorAttachments.size());
        for (size_t i = 0; i < colorAttachments.size(); ++i) {
            GLuint texName =
                static_cast<GLuint>(colorAttachments[i]->nativeHandle());
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0 + (GLenum)i,
                                   GL_TEXTURE_2D, texName, 0);
            drawBuffers[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
        }

        glDrawBuffers((GLsizei)drawBuffers.size(), drawBuffers.data());

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            LOGE("createMRTFramebuffer: incomplete (0x%x); mixed formats?",
                 status);
            glDeleteFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return nullptr;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return std::make_unique<GlesFramebuffer>(fbo, width, height);
    }

private:
    EGLContext eglContext_;
    EGLDisplay eglDisplay_;
    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    int maxTextureSize_ = 0;
    std::unique_ptr<GlesShaderProgram> blitShader_;  // lazy; see blit()
};

// -----------------------------------------------------------------------------
// Factory
// -----------------------------------------------------------------------------
std::unique_ptr<RenderContext> createGlesRenderContext(void* eglCtx, void* eglDpy) {
    return std::make_unique<GlesRenderContext>(
        static_cast<EGLContext>(eglCtx),
        static_cast<EGLDisplay>(eglDpy));
}

}  // namespace community_ar

#endif  // __ANDROID__
