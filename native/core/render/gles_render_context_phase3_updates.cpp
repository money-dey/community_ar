// gles_render_context_phase3_updates.cpp
// =============================================================================
// GLES implementations of the render context methods added in Phase 2
// (retroactively) and Phase 3.
//
// As with other Phase N updates files in this project, this is a delta
// document — the actual code lives merged into gles_render_context.cpp
// alongside the existing GLES backend. This file exists so the additions
// are reviewable in isolation.
//
// Adds:
//   - drawTriangles()         — Phase 2 mask rasterizer needed this
//   - createMRTFramebuffer()  — Phase 3 multiclass channel splitter
// =============================================================================

#include "render_context_additions.h"
#include <GLES3/gl3.h>
#include <android/log.h>
#include <cassert>

namespace community_ar {

// (These methods are members of GLESRenderContext, which inherits from
// RenderContextEx. The class declaration lives in gles_render_context.cpp.)

// -----------------------------------------------------------------------------
// drawTriangles
//
// Wraps glDrawArrays(GL_TRIANGLES, ...). The shader and vertex buffer must
// already be bound by the caller (via shader->use() and the appropriate
// attribute setup) — this method is just the draw call itself.
// -----------------------------------------------------------------------------
void GLESRenderContext::drawTriangles(ShaderProgram* shader,
                                       VertexBuffer* vbo,
                                       int firstVertex,
                                       int vertexCount) {
    assert(shader && vbo);
    assert(vertexCount > 0 && vertexCount % 3 == 0);

    // Bind the VBO. The GLESVertexBuffer's nativeHandle() returns its GL name.
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(vbo->nativeHandle()));

    // Attribute layout: position (location 0, 2 floats) + alpha (location 1,
    // 1 float), interleaved. This matches the mask rasterizer's vertex
    // format. If we add new triangle-list use cases with different formats,
    // we'll need to push attribute setup into the caller or take a layout
    // descriptor argument.
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

// -----------------------------------------------------------------------------
// createMRTFramebuffer
//
// Creates a GL_FRAMEBUFFER with N color attachments, each pointing at one
// of the provided textures. Configures glDrawBuffers() so the fragment
// shader's location-bound outputs write to the correct attachments.
//
// Validation:
//   - All attachments must be the same size (GL_FRAMEBUFFER_INCOMPLETE_*
//     errors if they're not — we check explicitly and log a clear message
//     for easier debugging)
//   - Attachment count must not exceed GL_MAX_COLOR_ATTACHMENTS (queried
//     once, cached)
// -----------------------------------------------------------------------------
std::unique_ptr<Framebuffer> GLESRenderContext::createMRTFramebuffer(
        const std::vector<const TextureHandle*>& colorAttachments) {
    if (colorAttachments.empty()) {
        __android_log_print(ANDROID_LOG_ERROR, "CommunityAR",
                            "createMRTFramebuffer: empty attachments list");
        return nullptr;
    }

    // Query max attachments once
    static GLint sMaxAttachments = 0;
    if (sMaxAttachments == 0) {
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &sMaxAttachments);
    }
    if ((GLint)colorAttachments.size() > sMaxAttachments) {
        __android_log_print(ANDROID_LOG_ERROR, "CommunityAR",
            "createMRTFramebuffer: %zu attachments requested, max is %d",
            colorAttachments.size(), sMaxAttachments);
        return nullptr;
    }

    // Verify all attachments have the same size
    int width = 0, height = 0;
    for (size_t i = 0; i < colorAttachments.size(); ++i) {
        const TextureHandle* tex = colorAttachments[i];
        if (!tex) {
            __android_log_print(ANDROID_LOG_ERROR, "CommunityAR",
                "createMRTFramebuffer: null attachment at index %zu", i);
            return nullptr;
        }
        if (i == 0) {
            width = tex->width();
            height = tex->height();
        } else if (tex->width() != width || tex->height() != height) {
            __android_log_print(ANDROID_LOG_ERROR, "CommunityAR",
                "createMRTFramebuffer: attachment %zu size mismatch "
                "(%dx%d vs %dx%d at index 0)",
                i, tex->width(), tex->height(), width, height);
            return nullptr;
        }
    }

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Attach each texture to GL_COLOR_ATTACHMENT0 + i and build the
    // drawBuffers list for glDrawBuffers().
    std::vector<GLenum> drawBuffers(colorAttachments.size());
    for (size_t i = 0; i < colorAttachments.size(); ++i) {
        GLuint texName = static_cast<GLuint>(colorAttachments[i]->nativeHandle());
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                                GL_COLOR_ATTACHMENT0 + (GLenum)i,
                                GL_TEXTURE_2D,
                                texName, 0);
        drawBuffers[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
    }

    glDrawBuffers((GLsizei)drawBuffers.size(), drawBuffers.data());

    // Validate completeness — this catches per-driver quirks (e.g. some
    // mobile GPUs reject mixed R8 + RGBA8 attachments).
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        __android_log_print(ANDROID_LOG_ERROR, "CommunityAR",
            "createMRTFramebuffer: status not complete (0x%x). "
            "Mixed formats? Falling back to single-attachment passes.",
            status);
        glDeleteFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return nullptr;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Wrap in our GLESFramebuffer RAII object. Constructor records the
    // GL name and size; destructor deletes the framebuffer (not the
    // attached textures — those have their own lifetime).
    return std::make_unique<GLESFramebuffer>(fbo, width, height);
}

}  // namespace community_ar
