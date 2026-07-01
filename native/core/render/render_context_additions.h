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
// Capabilities added in Phase 2 (retroactively documented here — the mask
// rasterizer needed them but they were never added to this header):
//   - Raw triangle draw (draws N triangles from a vertex buffer; distinct
//     from drawFullscreenQuad which always draws two triangles covering
//     the viewport)
//
// Capabilities added in Phase 3:
//   - Multi-render-target framebuffer creation (used by the multiclass
//     segmenter to split one tensor into 6 single-channel textures in one
//     pass; also useful for beauty v2's multi-band composition)
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

    // -------------------------------------------------------------------------
    // Phase 2 addition (retroactive)
    //
    // Draw raw triangles from a vertex buffer. Unlike drawFullscreenQuad,
    // this draws an arbitrary triangle list — used by mask_rasterizer to
    // emit landmark-contour triangle fans.
    //
    // - shader:      already-bound shader program
    // - vbo:         vertex buffer containing the triangle data; layout must
    //                match what shader expects (position + alpha for masks)
    // - firstVertex: index of the first vertex to draw
    // - vertexCount: number of vertices to draw (must be multiple of 3 for
    //                GL_TRIANGLES topology)
    // -------------------------------------------------------------------------
    virtual void drawTriangles(ShaderProgram* shader,
                               VertexBuffer* vbo,
                               int firstVertex,
                               int vertexCount) = 0;

    // -------------------------------------------------------------------------
    // Phase 3 addition
    //
    // Create a framebuffer with multiple color attachments (MRT — Multiple
    // Render Targets). A fragment shader bound to this framebuffer can
    // declare multiple `out` variables, one per attachment, and write all
    // outputs in a single pass.
    //
    // Use cases in the codebase:
    //   - multiclass_segmenter_backend: split 6-channel model output into
    //     6 separate R8 textures in one pass
    //   - (future) beauty v2 multi-band composition: write low-freq, mid-freq,
    //     and high-freq bands simultaneously
    //
    // Constraints:
    //   - All attachments must have the same width and height
    //   - All attachments must have compatible internal formats (most
    //     hardware allows mixing R8, RG8, RGBA8 freely; depth/stencil formats
    //     cannot be color attachments)
    //   - Max attachments: GL_MAX_DRAW_BUFFERS, typically 8 on Android GLES
    //     3.0+ and on iOS Metal. We rely on at most 6 for Phase 3.
    //
    // The textures must outlive the returned Framebuffer; the Framebuffer
    // holds non-owning references to them. Typically the caller keeps the
    // attached textures as members alongside the framebuffer.
    // -------------------------------------------------------------------------
    virtual std::unique_ptr<Framebuffer> createMRTFramebuffer(
        const std::vector<const TextureHandle*>& colorAttachments) = 0;
};

}  // namespace community_ar
