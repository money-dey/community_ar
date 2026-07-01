// render_context.h
// =============================================================================
// Community AR — RenderContext
//
// Platform-agnostic facade over the GPU device. Hides whether we're on
// OpenGL ES (Android) or Metal (iOS). All Effect implementations talk
// to this interface; never directly to GLES/Metal APIs.
//
// Lifetime: one RenderContext per Session. Created on the render thread,
// destroyed on the render thread. All methods must be called from the
// render thread (no thread-safety guarantees).
// =============================================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace community_ar {

// -----------------------------------------------------------------------------
// TextureHandle — RAII wrapper around a GPU texture
//
// On Android (GLES): backed by a GLuint texture name + EGLImage (if external).
// On iOS (Metal):    backed by an id<MTLTexture>.
//
// Texture handles can be:
//   - Owned: this object will release the GPU resource on destruction
//   - Borrowed: an external system (e.g., camera, Flutter texture registry)
//     owns the underlying resource; we just hold a reference
// =============================================================================
class TextureHandle {
public:
    enum class Format {
        RGBA8,
        RGBA16F,    // half-float, for HDR intermediate buffers
        R8,         // single-channel (masks)
        ExternalOES // Android camera texture (OES_EGL_image_external)
    };

    enum class Ownership { Owned, Borrowed };

    TextureHandle();
    TextureHandle(uint64_t nativeHandle, int w, int h, Format f, Ownership o);
    ~TextureHandle();

    TextureHandle(TextureHandle&& other) noexcept;
    TextureHandle& operator=(TextureHandle&& other) noexcept;

    TextureHandle(const TextureHandle&) = delete;
    TextureHandle& operator=(const TextureHandle&) = delete;

    bool valid() const { return nativeHandle_ != 0; }
    uint64_t nativeHandle() const { return nativeHandle_; }
    int width() const { return width_; }
    int height() const { return height_; }
    Format format() const { return format_; }

private:
    uint64_t nativeHandle_ = 0;
    int width_ = 0;
    int height_ = 0;
    Format format_ = Format::RGBA8;
    Ownership ownership_ = Ownership::Borrowed;
};

// -----------------------------------------------------------------------------
// Framebuffer — a render target
// -----------------------------------------------------------------------------
class Framebuffer {
public:
    virtual ~Framebuffer() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual const TextureHandle& colorTexture() const = 0;
    virtual uint64_t nativeHandle() const = 0;  // GLuint FBO or MTLRenderPassDescriptor*
};

// -----------------------------------------------------------------------------
// ShaderProgram — compiled GPU program
// -----------------------------------------------------------------------------
class ShaderProgram {
public:
    virtual ~ShaderProgram() = default;

    virtual void use() = 0;
    virtual void setUniform(const char* name, float v) = 0;
    virtual void setUniform(const char* name, float x, float y) = 0;
    virtual void setUniform(const char* name, float x, float y, float z) = 0;
    virtual void setUniform(const char* name, float x, float y, float z, float w) = 0;
    virtual void setUniform(const char* name, int v) = 0;
    virtual void setUniformMatrix4(const char* name, const float* values) = 0;
    virtual void bindTexture(const char* samplerName, const TextureHandle& tex, int unit) = 0;
};

// -----------------------------------------------------------------------------
// RenderContext — the top-level interface
// -----------------------------------------------------------------------------
class RenderContext {
public:
    virtual ~RenderContext() = default;

    // -- Resource creation --
    virtual std::unique_ptr<TextureHandle> createTexture(int w, int h,
                                                        TextureHandle::Format format) = 0;
    virtual std::unique_ptr<Framebuffer> createFramebuffer(int w, int h,
                                                          TextureHandle::Format format) = 0;
    virtual std::unique_ptr<ShaderProgram> createShader(const std::string& vertexSrc,
                                                       const std::string& fragmentSrc) = 0;

    // -- Render commands --
    virtual void bindFramebuffer(Framebuffer* fbo) = 0;  // null = default framebuffer
    virtual void clearColor(float r, float g, float b, float a) = 0;
    virtual void drawFullscreenQuad(ShaderProgram* program) = 0;
    virtual void blit(const TextureHandle& src, Framebuffer* dst) = 0;

    // -- Sync --
    virtual void flush() = 0;        // submit queued GPU work
    virtual void waitGpu() = 0;      // block until GPU is idle (use sparingly)

    // -- Introspection --
    virtual const char* backendName() const = 0;
    virtual int maxTextureSize() const = 0;
};

// -----------------------------------------------------------------------------
// Platform-specific factory functions
// -----------------------------------------------------------------------------
#if defined(__ANDROID__)
std::unique_ptr<RenderContext> createGlesRenderContext(void* eglContext, void* eglDisplay);
#endif

#if defined(__APPLE__)
std::unique_ptr<RenderContext> createMetalRenderContext(void* mtlDevice);
#endif

}  // namespace community_ar
