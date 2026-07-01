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
#include <vector>

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
// VertexBuffer — RAII wrapper around a GPU vertex buffer (VBO on GLES,
// MTLBuffer on Metal). Folded in from render_context_additions.h.
// -----------------------------------------------------------------------------
class VertexBuffer {
public:
    virtual ~VertexBuffer() = default;
    virtual uint64_t nativeHandle() const = 0;
    virtual size_t   sizeBytes() const = 0;
};

// Describes how per-vertex and per-instance attribute data are laid out.
struct InstancedAttr {
    int location;   // layout(location = N) in the shader
    int size;       // 1..4 floats
    int offset;     // bytes within the stride
};

struct InstancedVertexFormat {
    int perVertexStride;
    std::vector<InstancedAttr> perVertexAttrs;
    int perInstanceStride;
    std::vector<InstancedAttr> perInstanceAttrs;
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

    // -------------------------------------------------------------------------
    // Extended API (folded in from render_context_additions.h / RenderContextEx).
    //
    // These are declared NON-pure with default no-op / null implementations so
    // that concrete backends stay instantiable while only some of them are
    // implemented. Backends override the ones they support; callers that need
    // an unimplemented capability get a null/no-op and should degrade.
    //
    // Implementation status at consolidation time:
    //   - drawTriangles / createMRTFramebuffer .... implemented by GLES + Metal
    //                                               (in *_phase3_updates files)
    //   - all others .............................. NOT yet implemented on any
    //                                               backend; default stubs here.
    //     Notably createFramebufferForTexture is required by the effect-chain
    //     ping-pong FBOs and beauty resource allocation but has no backend
    //     implementation — a pre-existing gap surfaced by this consolidation.
    // -------------------------------------------------------------------------
    virtual std::unique_ptr<VertexBuffer> createVertexBuffer(
        const void* data, size_t bytes) { return nullptr; }

    virtual std::unique_ptr<VertexBuffer> createDynamicVertexBuffer(
        size_t maxBytes) { return nullptr; }

    virtual void uploadDynamicVertexBuffer(VertexBuffer* vbo,
                                           const void* data, size_t bytes) {}

    virtual std::unique_ptr<ShaderProgram> createInstancedShader(
        const std::string& vertexSrc,
        const std::string& fragmentSrc,
        const InstancedVertexFormat& fmt) { return nullptr; }

    virtual void drawInstancedQuads(ShaderProgram* program,
                                    VertexBuffer* perVertex, int vertexCount,
                                    VertexBuffer* perInstance,
                                    int instanceCount) {}

    // Create a framebuffer that renders into an EXISTING owned texture.
    virtual std::unique_ptr<Framebuffer> createFramebufferForTexture(
        TextureHandle& tex) { return nullptr; }

    virtual void enableAlphaBlending(bool enable) {}

    virtual void currentFramebufferSize(int* outW, int* outH) const {}

    // Draw an arbitrary triangle list from a vertex buffer (mask rasterizer).
    virtual void drawTriangles(ShaderProgram* shader,
                               VertexBuffer* vbo,
                               int firstVertex,
                               int vertexCount) {}

    // Create a framebuffer with multiple color attachments (MRT).
    virtual std::unique_ptr<Framebuffer> createMRTFramebuffer(
        const std::vector<const TextureHandle*>& colorAttachments) {
        return nullptr;
    }
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
