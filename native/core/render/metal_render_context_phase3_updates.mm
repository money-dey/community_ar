// metal_render_context_phase3_updates.mm
// =============================================================================
// Metal implementations of the render context methods added in Phase 2
// (retroactively) and Phase 3.
//
// Delta document — actual code merges into metal_render_context.mm.
//
// On iOS, MRT is supported natively via MTLRenderPassDescriptor's
// colorAttachments indexed property. The fragment shader uses MSL's
// [[color(N)]] attribute on output struct fields to target attachments.
// SPIRV-Cross will produce the right MSL when we unify shader sources
// in Phase 5; for now, the channel-split shader has a hand-written MSL
// counterpart living next to its GLSL.
// =============================================================================

#import "render_context_additions.h"
#import <Metal/Metal.h>
#import <os/log.h>

namespace community_ar {

// -----------------------------------------------------------------------------
// drawTriangles (Metal)
//
// Encoded into the current command buffer's render command encoder.
// Caller must have already set the pipeline state (via shader->use()) and
// bound any uniforms.
// -----------------------------------------------------------------------------
void MetalRenderContext::drawTriangles(ShaderProgram* shader,
                                        VertexBuffer* vbo,
                                        int firstVertex,
                                        int vertexCount) {
    id<MTLRenderCommandEncoder> encoder = currentEncoder_;
    if (!encoder) {
        os_log_error(OS_LOG_DEFAULT,
                     "drawTriangles called with no active encoder");
        return;
    }

    id<MTLBuffer> mtlBuffer =
        (__bridge id<MTLBuffer>)(void*)vbo->nativeHandle();
    [encoder setVertexBuffer:mtlBuffer offset:0 atIndex:0];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:firstVertex
                vertexCount:vertexCount];
}

// -----------------------------------------------------------------------------
// createMRTFramebuffer (Metal)
//
// On Metal, a "framebuffer" is conceptually a MTLRenderPassDescriptor —
// not a long-lived object like an OpenGL framebuffer, but a descriptor
// configured each time we begin a render pass. To match the cross-platform
// Framebuffer abstraction, we store the descriptor as a member of the
// MetalFramebuffer object and re-use it on each bindFramebuffer() call.
//
// Trade-off vs GL: Metal does not have the GL "incomplete framebuffer"
// failure mode — invalid configurations fail later at encoder creation.
// We do early validation (same-size attachments) for parity with the GL
// path so callers see consistent error reporting.
// -----------------------------------------------------------------------------
std::unique_ptr<Framebuffer> MetalRenderContext::createMRTFramebuffer(
        const std::vector<const TextureHandle*>& colorAttachments) {
    if (colorAttachments.empty()) {
        os_log_error(OS_LOG_DEFAULT,
                     "createMRTFramebuffer: empty attachments list");
        return nullptr;
    }
    if (colorAttachments.size() > 8) {
        // Metal limits to 8 color attachments per render pass on iOS GPUs.
        os_log_error(OS_LOG_DEFAULT,
                     "createMRTFramebuffer: %zu attachments exceeds Metal limit of 8",
                     colorAttachments.size());
        return nullptr;
    }

    // Verify uniform size
    int width = 0, height = 0;
    for (size_t i = 0; i < colorAttachments.size(); ++i) {
        const TextureHandle* tex = colorAttachments[i];
        if (!tex) return nullptr;
        if (i == 0) { width = tex->width(); height = tex->height(); }
        else if (tex->width() != width || tex->height() != height) {
            os_log_error(OS_LOG_DEFAULT,
                "createMRTFramebuffer: attachment %zu size mismatch", i);
            return nullptr;
        }
    }

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    for (size_t i = 0; i < colorAttachments.size(); ++i) {
        id<MTLTexture> mtlTex =
            (__bridge id<MTLTexture>)(void*)colorAttachments[i]->nativeHandle();
        desc.colorAttachments[i].texture = mtlTex;
        desc.colorAttachments[i].loadAction  = MTLLoadActionLoad;
        desc.colorAttachments[i].storeAction = MTLStoreActionStore;
    }

    // Wrap in our MetalFramebuffer object. Holds the descriptor and the
    // size for queries.
    return std::make_unique<MetalFramebuffer>(desc, width, height);
}

}  // namespace community_ar
