// render_context_additions.h
// =============================================================================
// Community AR — RenderContext production additions
//
// Capabilities added to RenderContext in Phase 1 fixes:
//   - Explicit VertexBuffer resource type (replaces inline kQuad arrays)
//   - Dynamic vertex buffers (uploaded each frame for instance data)
//   - Instanced rendering primitive
//   - Framebuffer-for-existing-texture (so the TFLite blitter can render
//     into a model-input texture without allocating its own attachment)
//   - Alpha-blending toggle
//   - Viewport reflection
//
// These should be folded into render_context.h proper — this file documents
// the deltas for the fix series.
// =============================================================================

#pragma once

#include "render_context.h"
#include <vector>

namespace community_ar {

// -----------------------------------------------------------------------------
// VertexBuffer — RAII wrapper around a GPU vertex buffer (VBO on GLES,
// MTLBuffer on Metal).
// -----------------------------------------------------------------------------
class VertexBuffer {
public:
    virtual ~VertexBuffer() = default;
    virtual uint64_t nativeHandle() const = 0;
    virtual size_t   sizeBytes() const = 0;
};

// -----------------------------------------------------------------------------
// InstancedVertexFormat — describes how per-vertex and per-instance attribute
// data are laid out in their respective buffers.
// -----------------------------------------------------------------------------
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
// RenderContext additions (would be merged into RenderContext interface)
// -----------------------------------------------------------------------------
class RenderContextEx : public RenderContext {
public:
    virtual std::unique_ptr<VertexBuffer> createVertexBuffer(
        const void* data, size_t bytes) = 0;

    virtual std::unique_ptr<VertexBuffer> createDynamicVertexBuffer(
        size_t maxBytes) = 0;

    virtual void uploadDynamicVertexBuffer(VertexBuffer* vbo,
                                           const void* data, size_t bytes) = 0;

    // An instanced-aware shader needs to know the vertex format up front so
    // it can wire attribute bindings on creation.
    virtual std::unique_ptr<ShaderProgram> createInstancedShader(
        const std::string& vertexSrc,
        const std::string& fragmentSrc,
        const InstancedVertexFormat& fmt) = 0;

    virtual void drawInstancedQuads(ShaderProgram* program,
                                    VertexBuffer* perVertex, int vertexCount,
                                    VertexBuffer* perInstance, int instanceCount) = 0;

    // Create a framebuffer that renders into an EXISTING owned texture.
    // Used by the TFLite blitter for OES→sampler2D conversion.
    virtual std::unique_ptr<Framebuffer> createFramebufferForTexture(
        TextureHandle& tex) = 0;

    virtual void enableAlphaBlending(bool enable) = 0;

    virtual void currentFramebufferSize(int* outW, int* outH) const = 0;
};

}  // namespace community_ar
