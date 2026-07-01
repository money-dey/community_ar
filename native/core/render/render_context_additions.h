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

namespace community_ar {

// -----------------------------------------------------------------------------
// CONSOLIDATED (Phase 3): the extended render API that used to live on a
// separate RenderContextEx subclass has been folded into the base
// RenderContext interface (see render_context.h). VertexBuffer,
// InstancedVertexFormat, drawTriangles, createMRTFramebuffer,
// createFramebufferForTexture, and the instanced-draw helpers are now base
// methods.
//
// RenderContextEx remains as a backward-compatibility alias so existing call
// sites — `static_cast<RenderContextEx*>(ctx)` and `RenderContextEx*` locals
// — keep compiling. New code should just use RenderContext directly.
// -----------------------------------------------------------------------------
using RenderContextEx = RenderContext;

}  // namespace community_ar
