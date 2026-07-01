// mask_rasterizer.h
// =============================================================================
// Community AR — Landmark contour → soft-edged mask
//
// Takes a closed polygonal contour from face landmarks and rasterizes it
// into a single-channel mask texture with soft edges. The mask values are:
//   - 1.0 deep inside the contour
//   - smoothly falling to 0.0 at and outside the boundary
//
// Approach:
//   1. CPU: triangulate the contour as a triangle fan around the centroid.
//   2. CPU: assign per-vertex alpha — 1.0 for the centroid, 0.0 for
//      boundary vertices.
//   3. GPU: rasterize the triangles with the alpha attribute interpolated.
//   4. GPU: fragment shader smoothstep()s the interpolated alpha by an
//      adjustable "edge softness" factor.
//
// The mask supports MULTIPLE contours per call:
//   - Additive contours: contribute mask = 1 (e.g. each face's outer lip)
//   - Subtractive contours: contribute mask = 0 (e.g. inner lip when mouth
//     is open, so we don't paint teeth)
//
// Memory: one mask texture per MaskRasterizer instance. Reused across
// frames; resized when the output FBO changes.
// =============================================================================

#pragma once

#include "../perception/perception_frame.h"
#include <memory>
#include <vector>

namespace community_ar {

class RenderContext;
class TextureHandle;
class Framebuffer;
class ShaderProgram;
class VertexBuffer;

// -----------------------------------------------------------------------------
// MaskContour — a single closed contour in normalized image coords [0,1]
// -----------------------------------------------------------------------------
struct MaskContour {
    std::vector<Vec2> points;   // closed polygon (last point connects to first)
    bool additive;              // true = adds to mask, false = subtracts from it
};

// -----------------------------------------------------------------------------
// MaskRasterizer
// -----------------------------------------------------------------------------
class MaskRasterizer {
public:
    explicit MaskRasterizer(RenderContext* ctx);
    ~MaskRasterizer();

    // Rasterize a set of contours into the mask texture.
    //
    // edgeSoftness: 0..1, controls how wide the soft edge band is.
    //   0.0 = razor sharp (visible polygon edges, do not use)
    //   0.3 = subtle softening, suitable for tight features (iris)
    //   0.5 = noticeable softening, suitable for lips/brows
    //   0.8 = very soft, suitable for skin or large regions
    //
    // outputWidth / outputHeight: size of the mask texture to produce.
    //   This is typically the camera frame's display size.
    void rasterize(const std::vector<MaskContour>& contours,
                   float edgeSoftness,
                   int outputWidth, int outputHeight);

    // Get the resulting mask texture. Valid until the next rasterize() call.
    const TextureHandle* mask() const { return maskTexture_.get(); }

private:
    void ensureResources(int width, int height);
    void buildTriangleFan(const MaskContour& contour, float baseAlpha,
                          std::vector<float>& outVertices) const;

    RenderContext* ctx_;
    std::unique_ptr<TextureHandle>  maskTexture_;
    std::unique_ptr<Framebuffer>    maskFbo_;
    std::unique_ptr<ShaderProgram>  rasterShader_;
    std::unique_ptr<VertexBuffer>   vertexBuffer_;   // dynamic, rebuilt per frame

    int currentWidth_  = 0;
    int currentHeight_ = 0;

    // CPU-side vertex scratch buffer (reused across frames)
    // Layout: x, y, alpha repeating (3 floats per vertex)
    std::vector<float> vertexScratch_;
};

}  // namespace community_ar
