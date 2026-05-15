// debug_overlay.h
// =============================================================================
// Community AR — Debug overlay (production)
//
// Renders perception output on top of the camera feed for visual verification.
// All landmark dots drawn in a single instanced draw call.
// =============================================================================

#pragma once

#include "../perception/perception_frame.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace community_ar {

class RenderContext;
class TextureHandle;
class Framebuffer;
class ShaderProgram;
class VertexBuffer;

enum class DebugOverlayMode : uint32_t {
    None       = 0,
    Landmarks  = 1 << 0,
    Mesh       = 1 << 1,
    Iris       = 1 << 2,
    HairMask   = 1 << 3,
    Pose       = 1 << 4,
    SkinTone   = 1 << 5,
    All        = 0xFFFFFFFF
};

struct DotInstance;  // forward; defined in .cpp

class DebugOverlay {
public:
    explicit DebugOverlay(RenderContext* ctx);
    ~DebugOverlay();

    void setMode(uint32_t modeMask) { modeMask_ = modeMask; }
    uint32_t mode() const { return modeMask_; }

    void render(const TextureHandle& inputCamera,
                const PerceptionFrame& frame,
                Framebuffer* outputFbo);

private:
    void ensureResources();

    RenderContext* ctx_;
    uint32_t modeMask_ = 0;

    std::unique_ptr<ShaderProgram> shaderPassthrough_;
    std::unique_ptr<ShaderProgram> shaderHairMask_;
    std::unique_ptr<ShaderProgram> shaderDots_;

    std::unique_ptr<VertexBuffer>  quadVerts_;     // shared per-vertex quad
    std::unique_ptr<VertexBuffer>  instanceVbo_;   // dynamic per-instance buffer

    std::vector<DotInstance> instanceBuffer_;
};

}  // namespace community_ar
