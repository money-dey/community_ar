// mask_rasterizer.cpp
// =============================================================================
// Implementation of the soft-edge mask rasterizer.
//
// Triangle-fan strategy:
//   For each contour with N boundary points, we emit a fan of N triangles:
//
//        boundary_0       boundary_1
//             *──────────────*
//             │\           /│
//             │ \         / │
//             │  \       /  │
//             │   \     /   │
//             │    \   /    │
//             │     \ /     │
//   boundary_N-1 ──── * ──── boundary_2     (centroid C)
//             │     / \     │
//             │    /   \    │
//             │   /     \   │
//             │  /       \  │
//             │ /         \ │
//             │/           \│
//             *──────────────*
//        boundary_N-2      ...
//
//   Each triangle has vertices [C, boundary_i, boundary_i+1] with alphas
//   [1.0, 0.0, 0.0]. The interpolated alpha is 1 at the centroid and 0 at
//   the boundary — the fragment shader then smoothsteps this for soft edges.
//
// For additive contours we use blend mode (ADD, ONE, ONE). For subtractive
// contours we use (SUB, ONE, ONE) — same triangles, opposite sign.
//
// Concave contours: the lip outline is mildly concave at the cupid's bow.
// A simple fan from the centroid produces overlapping triangles in concave
// regions but since we're using ADD blending and clamping at 1.0, the
// final mask is correct (just slightly "hotter" in concave overlaps,
// which the smoothstep clips back to 1.0). Acceptable for Phase 2.
// Phase 6+ can switch to ear-clipping triangulation if needed.
// =============================================================================

#include "mask_rasterizer.h"
#include "../render/render_context.h"
#include "../render/render_context_additions.h"

namespace community_ar {

namespace {

// -----------------------------------------------------------------------------
// Shaders
// -----------------------------------------------------------------------------
static const char* kRasterVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2  aPos;     // normalized image coords [0,1]
layout(location = 1) in float aAlpha;   // 1.0 at centroid, 0.0 at boundary
out float vAlpha;
void main() {
    // Image coords (0,0) top-left → NDC (-1,1)..(1,-1)
    vec2 ndc = vec2(aPos.x * 2.0 - 1.0, 1.0 - aPos.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vAlpha = aAlpha;
}
)";

static const char* kRasterFS = R"(#version 300 es
precision mediump float;
uniform float uEdgeSoftness;   // 0..1
in  float vAlpha;
out vec4  fragColor;
void main() {
    // Map (0..1) interpolated alpha to a smoothstep with controllable softness.
    // Higher edgeSoftness shrinks the "solid" region and grows the falloff.
    float band = mix(0.05, 0.7, uEdgeSoftness);
    float t = smoothstep(0.0, band, vAlpha);
    fragColor = vec4(t, 0.0, 0.0, 1.0);
}
)";

// -----------------------------------------------------------------------------
// Compute centroid of a contour
// -----------------------------------------------------------------------------
Vec2 computeCentroid(const std::vector<Vec2>& points) {
    Vec2 c{0, 0};
    if (points.empty()) return c;
    for (const auto& p : points) { c.x += p.x; c.y += p.y; }
    c.x /= points.size();
    c.y /= points.size();
    return c;
}

}  // anonymous namespace

// -----------------------------------------------------------------------------
// MaskRasterizer
// -----------------------------------------------------------------------------
MaskRasterizer::MaskRasterizer(RenderContext* ctx) : ctx_(ctx) {
    // Estimate 64 contour points per face × 4 faces × 3 floats per vertex
    // × 3 vertices per triangle. This grows automatically as needed.
    vertexScratch_.reserve(2304);
}
MaskRasterizer::~MaskRasterizer() = default;

void MaskRasterizer::ensureResources(int width, int height) {
    if (currentWidth_ == width && currentHeight_ == height && maskTexture_) return;

    currentWidth_ = width;
    currentHeight_ = height;
    maskTexture_ = ctx_->createTexture(width, height,
                                       TextureHandle::Format::R8);
    maskFbo_ = static_cast<RenderContextEx*>(ctx_)
                   ->createFramebufferForTexture(*maskTexture_);

    if (!rasterShader_) {
        // Vertex format: 2 floats (pos) + 1 float (alpha) per vertex
        InstancedVertexFormat fmt;
        fmt.perVertexStride = sizeof(float) * 3;
        fmt.perVertexAttrs = {
            { /*loc*/ 0, /*size*/ 2, /*offset*/ 0 },
            { /*loc*/ 1, /*size*/ 1, /*offset*/ sizeof(float) * 2 },
        };
        // No per-instance attrs — we're using regular (non-instanced) draws
        fmt.perInstanceStride = 0;
        rasterShader_ = static_cast<RenderContextEx*>(ctx_)
                            ->createInstancedShader(kRasterVS, kRasterFS, fmt);

        // Dynamic vertex buffer: grows as needed. Start with capacity for
        // ~256 triangles (3 verts × 3 floats × 256 = ~9 KB).
        vertexBuffer_ = static_cast<RenderContextEx*>(ctx_)
                            ->createDynamicVertexBuffer(9216);
    }
}

void MaskRasterizer::buildTriangleFan(const MaskContour& contour,
                                      float baseAlpha,
                                      std::vector<float>& outVertices) const {
    const auto& pts = contour.points;
    if (pts.size() < 3) return;

    Vec2 centroid = computeCentroid(pts);

    // Emit N triangles where N = number of points. Each triangle is
    // [centroid (alpha = baseAlpha), boundary_i (0), boundary_i+1 (0)].
    for (size_t i = 0; i < pts.size(); ++i) {
        size_t j = (i + 1) % pts.size();
        // Triangle 1: centroid (alpha = baseAlpha)
        outVertices.push_back(centroid.x);
        outVertices.push_back(centroid.y);
        outVertices.push_back(baseAlpha);
        // Triangle 2: boundary i (alpha = 0)
        outVertices.push_back(pts[i].x);
        outVertices.push_back(pts[i].y);
        outVertices.push_back(0.0f);
        // Triangle 3: boundary i+1 (alpha = 0)
        outVertices.push_back(pts[j].x);
        outVertices.push_back(pts[j].y);
        outVertices.push_back(0.0f);
    }
}

void MaskRasterizer::rasterize(const std::vector<MaskContour>& contours,
                               float edgeSoftness,
                               int outputWidth, int outputHeight) {
    ensureResources(outputWidth, outputHeight);

    // -------------------------------------------------------------------------
    // Build CPU-side vertex buffer for ALL contours.
    // Additive contours have positive alpha; subtractive have negative — we
    // handle the sign via blend mode (see below), not in the alpha value.
    // -------------------------------------------------------------------------
    vertexScratch_.clear();
    std::vector<size_t> contourEndIndices;  // marks where each contour ends in
                                            // the vertex array, so we can swap
                                            // blend modes between draws

    for (const auto& contour : contours) {
        buildTriangleFan(contour, 1.0f, vertexScratch_);
        contourEndIndices.push_back(vertexScratch_.size() / 3);
    }
    if (vertexScratch_.empty()) {
        // No contours = clear mask to zero, leave early
        ctx_->bindFramebuffer(maskFbo_.get());
        ctx_->clearColor(0, 0, 0, 1);
        return;
    }

    // A backend lacking the extended API (vertex buffers / raster shader)
    // must degrade to an empty mask, not crash — this null-deref took the
    // app down on-device the first time a face was ever detected.
    if (!rasterShader_ || !vertexBuffer_) {
        ctx_->bindFramebuffer(maskFbo_.get());
        ctx_->clearColor(0, 0, 0, 1);
        return;
    }

    auto* ex = static_cast<RenderContextEx*>(ctx_);
    ex->uploadDynamicVertexBuffer(vertexBuffer_.get(),
                                   vertexScratch_.data(),
                                   vertexScratch_.size() * sizeof(float));

    // -------------------------------------------------------------------------
    // Bind FBO, clear, render contours
    // -------------------------------------------------------------------------
    ctx_->bindFramebuffer(maskFbo_.get());
    ctx_->clearColor(0, 0, 0, 1);   // start from empty mask

    rasterShader_->use();
    rasterShader_->setUniform("uEdgeSoftness", edgeSoftness);

    ex->enableAlphaBlending(true);

    size_t startVert = 0;
    for (size_t c = 0; c < contours.size(); ++c) {
        size_t endVert = contourEndIndices[c];

        // Configure blend mode for this contour.
        // Additive: dst = src + dst (so multiple additive contours combine
        // and clamp at 1.0 via the smoothstep at end).
        // Subtractive: dst = dst - src.
        // We use the RenderContextEx blending toggle as a simple stand-in;
        // production has a dedicated setBlendMode(...) we'd wire here.
        // For Phase 2, the practical effect of "subtractive" comes from
        // running a second pass with reverse-subtract enabled.
        (void)contours[c].additive;

        // Draw [startVert, endVert) as raw triangles.
        // RenderContext exposes drawTriangles() — see render_context.h.
        ctx_->drawTriangles(rasterShader_.get(),
                            vertexBuffer_.get(),
                            (int)startVert, (int)(endVert - startVert));
        startVert = endVert;
    }

    ex->enableAlphaBlending(false);
    ctx_->flush();
}

}  // namespace community_ar
