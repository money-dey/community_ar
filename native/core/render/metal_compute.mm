// metal_compute.mm
// =============================================================================
// Metal compute primitives implementation (iOS).
//
// Metal's compute path is straightforward:
//   - MTLBuffer with MTLStorageModeShared is CPU + GPU coherent. No mapping,
//     no fence. Once the command buffer that wrote it has completed, the
//     CPU can read directly from buffer.contents.
//   - MTLComputePipelineState is the compiled compute program.
//   - dispatchThreadgroups encodes the dispatch.
//   - Async readback: we install an `addCompletedHandler:` on the command
//     buffer; the AsyncReadback handle flips to ready when the handler fires.
// =============================================================================

#if defined(__APPLE__)

#include "compute_primitives.h"
#import  <Metal/Metal.h>
#import  <Foundation/Foundation.h>
#include <atomic>
#include <os/log.h>

namespace community_ar {

// -----------------------------------------------------------------------------
// MetalShaderStorageBuffer
// -----------------------------------------------------------------------------
class MetalShaderStorageBuffer : public ShaderStorageBuffer {
public:
    MetalShaderStorageBuffer(id<MTLDevice> device, size_t bytes,
                             StorageMode mode, const void* initial)
        : sizeBytes_(bytes), mode_(mode) {
        MTLResourceOptions opts = (mode == StorageMode::Shared)
            ? MTLResourceStorageModeShared
            : MTLResourceStorageModePrivate;
        if (initial && mode == StorageMode::Shared) {
            buffer_ = (__bridge_retained void*)
                [device newBufferWithBytes:initial length:bytes options:opts];
        } else {
            buffer_ = (__bridge_retained void*)
                [device newBufferWithLength:bytes options:opts];
            if (initial && mode == StorageMode::GpuOnly) {
                // GpuOnly needs a staging copy. For Phase 1 we treat initial-
                // data with GpuOnly as a no-op since none of our compute work
                // pre-populates GPU-private buffers.
                os_log_info(OS_LOG_DEFAULT,
                    "GpuOnly buffer initial data ignored — use Shared mode for staging");
            }
        }
    }

    ~MetalShaderStorageBuffer() override {
        if (buffer_) {
            id<MTLBuffer> b = (__bridge_transfer id<MTLBuffer>)buffer_;
            (void)b;
        }
    }

    uint64_t nativeHandle() const override {
        return reinterpret_cast<uint64_t>(buffer_);
    }
    size_t   sizeBytes() const override { return sizeBytes_; }
    StorageMode storageMode() const override { return mode_; }

    id<MTLBuffer> mtlBuffer() const {
        return (__bridge id<MTLBuffer>)buffer_;
    }

private:
    void* buffer_ = nullptr;
    size_t sizeBytes_;
    StorageMode mode_;
};

// -----------------------------------------------------------------------------
// MetalComputeProgram
// -----------------------------------------------------------------------------
class MetalComputeProgram : public ComputeProgram {
public:
    MetalComputeProgram(id<MTLDevice> device, const std::string& msl) {
        NSError* err = nil;
        id<MTLLibrary> lib = [device newLibraryWithSource:
            [NSString stringWithUTF8String:msl.c_str()] options:nil error:&err];
        if (!lib) {
            os_log_error(OS_LOG_DEFAULT, "Compute shader compile failed: %{public}@", err);
            return;
        }
        // Convention: compute entry point is named "main_kernel". The skin
        // tone shader follows this convention; future shaders can be looked
        // up by inspecting library.functionNames if we want flexibility.
        id<MTLFunction> fn = [lib newFunctionWithName:@"main_kernel"];
        if (!fn) {
            os_log_error(OS_LOG_DEFAULT, "No 'main_kernel' function in compute source");
            return;
        }
        pipeline_ = (__bridge_retained void*)
            [device newComputePipelineStateWithFunction:fn error:&err];
        if (!pipeline_) {
            os_log_error(OS_LOG_DEFAULT, "Compute pipeline state failed: %{public}@", err);
        }
    }

    ~MetalComputeProgram() override {
        if (pipeline_) {
            id<MTLComputePipelineState> p = (__bridge_transfer id<MTLComputePipelineState>)pipeline_;
            (void)p;
        }
    }

    void use() override { /* set inside encoder by dispatchCompute */ }

    void bindStorageBuffer(int bindingPoint, ShaderStorageBuffer* buf) override {
        if (!buf) return;
        boundBuffers_.resize(std::max((int)boundBuffers_.size(), bindingPoint + 1));
        boundBuffers_[bindingPoint] = static_cast<MetalShaderStorageBuffer*>(buf);
    }

    void bindTexture(const char*, const TextureHandle& tex, int unit) override {
        boundTextures_.resize(std::max((int)boundTextures_.size(), unit + 1));
        boundTextures_[unit] = (__bridge id<MTLTexture>)
            reinterpret_cast<void*>(tex.nativeHandle());
    }

    void setUniform(const char* n, float v) override  { uniformsFloat_[n] = v; }
    void setUniform(const char* n, int v) override    { uniformsInt_[n] = v; }
    void setUniform(const char* n, float x, float y) override {
        uniformsVec2_[n] = {x, y};
    }

    id<MTLComputePipelineState> pipelineState() const {
        return (__bridge id<MTLComputePipelineState>)pipeline_;
    }

    // Called by dispatchCompute to actually encode all the bindings
    void encodeBindings(id<MTLComputeCommandEncoder> encoder) const {
        // Storage buffers at buffer(0..N)
        for (size_t i = 0; i < boundBuffers_.size(); ++i) {
            if (boundBuffers_[i])
                [encoder setBuffer:boundBuffers_[i]->mtlBuffer() offset:0 atIndex:i];
        }
        // Textures at texture(0..N)
        for (size_t i = 0; i < boundTextures_.size(); ++i) {
            if (boundTextures_[i]) [encoder setTexture:boundTextures_[i] atIndex:i];
        }
        // Scalar uniforms — packed into a small constant buffer.
        // For Phase 1, we pack on the fly into a single struct at buffer
        // slot 15 (above the storage-buffer range we use). Shaders are
        // authored to read uniforms from that slot.
        if (!uniformsFloat_.empty() || !uniformsInt_.empty() || !uniformsVec2_.empty()) {
            // For now, the skin tone compute shader uses no uniforms beyond
            // texture/buffer bindings. When we add other compute passes
            // requiring uniforms, we'll fill in this packing step.
        }
    }

private:
    void* pipeline_ = nullptr;
    std::vector<MetalShaderStorageBuffer*> boundBuffers_;
    std::vector<id<MTLTexture>> boundTextures_;
    std::unordered_map<std::string, float> uniformsFloat_;
    std::unordered_map<std::string, int>   uniformsInt_;
    struct Vec2 { float x, y; };
    std::unordered_map<std::string, Vec2>  uniformsVec2_;
};

// -----------------------------------------------------------------------------
// MetalAsyncReadback
// -----------------------------------------------------------------------------
class MetalAsyncReadback : public AsyncReadback {
public:
    MetalAsyncReadback(id<MTLCommandBuffer> cb, MetalShaderStorageBuffer* buf,
                       size_t bytes)
        : buffer_(buf), bytes_(bytes) {
        // The Shared-mode buffer is CPU-coherent. Once the command buffer
        // is `completed`, the buffer's contents are visible to the CPU.
        ready_ = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<bool>> r = ready_;
        [cb addCompletedHandler:^(id<MTLCommandBuffer>) {
            r->store(true);
        }];
    }

    bool isReady() const override { return ready_ && ready_->load(); }
    const void* data() const override {
        return buffer_ ? buffer_->mtlBuffer().contents : nullptr;
    }
    size_t size() const override { return bytes_; }
    void release() override {
        ready_.reset();
        buffer_ = nullptr;
    }

private:
    MetalShaderStorageBuffer* buffer_;
    size_t bytes_;
    std::shared_ptr<std::atomic<bool>> ready_;
};

// -----------------------------------------------------------------------------
// RenderContext compute method implementations (members of MetalRenderContext
// in production; kept free-standing here for diff clarity)
// -----------------------------------------------------------------------------
namespace metal_compute_impl {

// These need an active id<MTLDevice> and id<MTLCommandQueue>; the production
// integration captures both in the MetalRenderContext and passes via 'self'.
// For the standalone delta file we declare prototypes; the actual MetalRender
// Context.mm hosts the wired-up versions.

struct MetalComputeContext {
    id<MTLDevice>       device;
    id<MTLCommandQueue> queue;
    // Current command buffer for the frame (created in dispatchCompute,
    // committed in storageBufferBarrier or at frame end).
    id<MTLCommandBuffer> currentCB;
    id<MTLComputeCommandEncoder> currentEncoder;
};

std::unique_ptr<ShaderStorageBuffer> createStorageBuffer(
        MetalComputeContext& mc, size_t bytes,
        ShaderStorageBuffer::StorageMode mode, const void* initial) {
    return std::make_unique<MetalShaderStorageBuffer>(mc.device, bytes, mode, initial);
}

std::unique_ptr<ComputeProgram> createComputeProgram(
        MetalComputeContext& mc, const std::string& msl) {
    return std::make_unique<MetalComputeProgram>(mc.device, msl);
}

void dispatchCompute(MetalComputeContext& mc, ComputeProgram* program,
                     int gx, int gy, int gz) {
    if (!program) return;
    auto* mp = static_cast<MetalComputeProgram*>(program);
    if (!mc.currentCB) mc.currentCB = [mc.queue commandBuffer];
    mc.currentEncoder = [mc.currentCB computeCommandEncoder];
    [mc.currentEncoder setComputePipelineState:mp->pipelineState()];
    mp->encodeBindings(mc.currentEncoder);
    MTLSize threadgroupSize = MTLSizeMake(32, 1, 1);
    MTLSize gridSize = MTLSizeMake(gx, gy, gz);
    [mc.currentEncoder dispatchThreadgroups:gridSize threadsPerThreadgroup:threadgroupSize];
    [mc.currentEncoder endEncoding];
    mc.currentEncoder = nil;
}

void storageBufferBarrier(MetalComputeContext& /*mc*/) {
    // No explicit barrier needed — Metal command buffers serialize sequential
    // encoders' resource accesses automatically.
}

std::unique_ptr<AsyncReadback> requestReadback(
        MetalComputeContext& mc, ShaderStorageBuffer* src, size_t bytes) {
    if (!src || !mc.currentCB) return nullptr;
    auto* msb = static_cast<MetalShaderStorageBuffer*>(src);
    [mc.currentCB commit];     // submit the work whose result we want to read
    auto rb = std::make_unique<MetalAsyncReadback>(mc.currentCB, msb, bytes);
    mc.currentCB = nil;        // next dispatch creates a new command buffer
    return rb;
}

}  // namespace metal_compute_impl

}  // namespace community_ar

#endif  // __APPLE__
