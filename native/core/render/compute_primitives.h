// compute_primitives.h
// =============================================================================
// Community AR — RenderContext compute primitives
//
// Phase 1 second-round additions. Adds compute shader support and async
// buffer readback to the RenderContext abstraction. These additions are
// shared between platforms; concrete implementations differ as follows:
//
//   OpenGL ES 3.1+ (Android):
//     - ShaderStorageBuffer = SSBO
//     - ComputeProgram      = compute shader linked into a program
//     - dispatch            = glDispatchCompute
//     - async readback      = glMapBufferRange + glFenceSync
//
//   Metal (iOS):
//     - ShaderStorageBuffer = id<MTLBuffer> with MTLStorageModeShared
//     - ComputeProgram      = id<MTLComputePipelineState> + MSL function
//     - dispatch            = compute encoder dispatchThreadgroups
//     - async readback      = MTLBuffer is CPU-coherent; just wait for the
//                              command buffer to complete via completion handler
//
// The interface is designed so the same compute shader source (with minor
// dialect tweaks handled via SPIRV-Cross later) can target both backends.
// For Phase 1 we accept GLSL ES 3.1 compute source on Android and provide
// a parallel MSL string for iOS — same as the Phase 0 test shaders.
// =============================================================================

#pragma once

#include "render_context.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace community_ar {

// -----------------------------------------------------------------------------
// ShaderStorageBuffer — a GPU buffer usable as both compute input and output.
//
// Two storage modes:
//   GpuOnly  — fastest; CPU cannot read directly. Use for transient compute
//              intermediates.
//   Shared   — CPU-readable. Slightly slower (CPU-coherent allocation).
//              Use for the destination of any async readback.
// -----------------------------------------------------------------------------
class ShaderStorageBuffer {
public:
    enum class StorageMode { GpuOnly, Shared };

    virtual ~ShaderStorageBuffer() = default;
    virtual uint64_t nativeHandle() const = 0;
    virtual size_t   sizeBytes() const = 0;
    virtual StorageMode storageMode() const = 0;
};

// -----------------------------------------------------------------------------
// ComputeProgram — compiled compute shader ready for dispatch.
// -----------------------------------------------------------------------------
class ComputeProgram {
public:
    virtual ~ComputeProgram() = default;

    virtual void use() = 0;

    // Bind a storage buffer to a layout(binding = N) slot in the shader.
    virtual void bindStorageBuffer(int bindingPoint, ShaderStorageBuffer* buf) = 0;

    // Bind a texture to a sampler uniform for the shader to read.
    virtual void bindTexture(const char* samplerName,
                             const TextureHandle& tex, int unit) = 0;

    // Set scalar uniforms (limited subset of the regular ShaderProgram surface).
    virtual void setUniform(const char* name, float v) = 0;
    virtual void setUniform(const char* name, int v) = 0;
    virtual void setUniform(const char* name, float x, float y) = 0;
};

// -----------------------------------------------------------------------------
// AsyncReadback — handle representing an in-flight CPU readback.
//
// The render thread issues a readback after a compute dispatch. The result
// becomes available some frames later. Polling pattern:
//
//   AsyncReadback rb = ctx->requestReadback(buf, dstBytes);
//   ... later frames ...
//   if (rb.isReady()) {
//       std::memcpy(out, rb.data(), rb.size());
//       rb.release();
//   }
//
// Lifetime: the AsyncReadback owns a pinned mapping; release() frees it.
// =============================================================================
class AsyncReadback {
public:
    virtual ~AsyncReadback() = default;
    virtual bool   isReady() const = 0;
    virtual const void* data() const = 0;
    virtual size_t      size() const = 0;
    virtual void   release() = 0;
};

// -----------------------------------------------------------------------------
// RenderContext additions (would be merged into the canonical interface)
// -----------------------------------------------------------------------------
class RenderContextCompute : public RenderContext {
public:
    // Resource creation
    virtual std::unique_ptr<ShaderStorageBuffer> createStorageBuffer(
        size_t bytes, ShaderStorageBuffer::StorageMode mode,
        const void* initialData = nullptr) = 0;

    virtual std::unique_ptr<ComputeProgram> createComputeProgram(
        const std::string& computeSource) = 0;

    // Dispatch — invokes (groupsX * groupsY * groupsZ) workgroups
    virtual void dispatchCompute(ComputeProgram* program,
                                 int groupsX, int groupsY = 1, int groupsZ = 1) = 0;

    // Memory barrier so subsequent reads observe the writes of the compute
    // pass. Conceptually GL_SHADER_STORAGE_BARRIER_BIT on Android; on Metal
    // it's an implicit dependency captured by the command buffer.
    virtual void storageBufferBarrier() = 0;

    // Issue an async readback from a shared-mode storage buffer.
    // Returns a handle whose isReady() flips true once the GPU has caught up.
    virtual std::unique_ptr<AsyncReadback> requestReadback(
        ShaderStorageBuffer* src, size_t bytes) = 0;
};

}  // namespace community_ar
