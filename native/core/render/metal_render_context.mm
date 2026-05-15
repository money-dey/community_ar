// metal_render_context.mm
// =============================================================================
// Metal implementation of RenderContext. Used on iOS.
//
// Phase 0 takes a pragmatic shortcut: rather than transpile our GLSL ES
// shaders to MSL via SPIRV-Cross right away, we keep an MSL version of each
// Phase 0 test shader inline. The full ABI will switch to SPIRV-Cross-based
// transpilation in a later phase so we maintain ONE shader source per effect.
//
// Camera frames arrive as CVMetalTextureRef wrapped in MTLTexture — already
// on the GPU with zero copy from AVCaptureSession output.
// =============================================================================

#if defined(__APPLE__)

#include "render_context.h"
#import  <Metal/Metal.h>
#import  <MetalKit/MetalKit.h>
#include <os/log.h>

namespace community_ar {

// -----------------------------------------------------------------------------
// TextureHandle implementation
//
// Native handle is a (__bridge_retained) id<MTLTexture> reinterpreted as
// uint64_t. Ownership::Owned means we hold a strong reference and must
// release on destruction. Borrowed means the caller retains ownership.
// -----------------------------------------------------------------------------
TextureHandle::TextureHandle() = default;

TextureHandle::TextureHandle(uint64_t handle, int w, int h, Format f, Ownership o)
    : nativeHandle_(handle), width_(w), height_(h), format_(f), ownership_(o) {}

TextureHandle::~TextureHandle() {
    if (ownership_ == Ownership::Owned && nativeHandle_ != 0) {
        id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>)
            reinterpret_cast<void*>(nativeHandle_);
        (void)tex;  // released by ARC
    }
}

TextureHandle::TextureHandle(TextureHandle&& o) noexcept
    : nativeHandle_(o.nativeHandle_), width_(o.width_), height_(o.height_),
      format_(o.format_), ownership_(o.ownership_) {
    o.nativeHandle_ = 0;
}

TextureHandle& TextureHandle::operator=(TextureHandle&& o) noexcept {
    if (this != &o) {
        if (ownership_ == Ownership::Owned && nativeHandle_ != 0) {
            id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>)
                reinterpret_cast<void*>(nativeHandle_);
            (void)tex;
        }
        nativeHandle_ = o.nativeHandle_;
        width_ = o.width_; height_ = o.height_;
        format_ = o.format_; ownership_ = o.ownership_;
        o.nativeHandle_ = 0;
    }
    return *this;
}

// -----------------------------------------------------------------------------
// MetalFramebuffer — an MTLTexture used as a render target
// -----------------------------------------------------------------------------
class MetalFramebuffer : public Framebuffer {
public:
    MetalFramebuffer(id<MTLDevice> device, int w, int h, TextureHandle::Format format)
        : w_(w), h_(h) {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                         width:w height:h mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        id<MTLTexture> tex = [device newTextureWithDescriptor:desc];
        texture_ = (__bridge_retained void*)tex;
        colorTexture_ = TextureHandle(
            reinterpret_cast<uint64_t>(texture_),
            w, h, format, TextureHandle::Ownership::Borrowed);
        // The Framebuffer holds the strong ref; colorTexture_ is borrowed from us.
    }

    ~MetalFramebuffer() override {
        if (texture_) {
            id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>)texture_;
            (void)tex;
        }
    }

    int width() const override { return w_; }
    int height() const override { return h_; }
    const TextureHandle& colorTexture() const override { return colorTexture_; }
    uint64_t nativeHandle() const override {
        return reinterpret_cast<uint64_t>(texture_);
    }

    id<MTLTexture> mtlTexture() const {
        return (__bridge id<MTLTexture>)texture_;
    }

private:
    int w_, h_;
    void* texture_ = nullptr;
    TextureHandle colorTexture_;
};

// -----------------------------------------------------------------------------
// MetalShaderProgram — wraps an MTLRenderPipelineState
//
// Compiled from MSL strings. In Phase 0 we accept GLSL source and translate
// the small fixed set of test shaders to inline MSL equivalents.
// -----------------------------------------------------------------------------
static const char* kMslLibrarySource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VertexIn  { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; };
struct VertexOut { float4 pos [[position]]; float2 uv; };

vertex VertexOut passthrough_vs(VertexIn in [[stage_in]]) {
    VertexOut out;
    out.pos = float4(in.pos, 0.0, 1.0);
    out.uv  = in.uv;
    return out;
}

fragment float4 passthrough_fs(VertexOut in [[stage_in]],
                               texture2d<float> tex [[texture(0)]],
                               sampler s [[sampler(0)]]) {
    return tex.sample(s, in.uv);
}

fragment float4 grayscale_fs(VertexOut in [[stage_in]],
                             texture2d<float> tex [[texture(0)]],
                             sampler s [[sampler(0)]]) {
    float3 c = tex.sample(s, in.uv).rgb;
    float g = dot(c, float3(0.2126, 0.7152, 0.0722));
    return float4(float3(g), 1.0);
}

fragment float4 invert_fs(VertexOut in [[stage_in]],
                          texture2d<float> tex [[texture(0)]],
                          sampler s [[sampler(0)]]) {
    float3 c = tex.sample(s, in.uv).rgb;
    return float4(1.0 - c, 1.0);
}

fragment float4 vignette_fs(VertexOut in [[stage_in]],
                            texture2d<float> tex [[texture(0)]],
                            sampler s [[sampler(0)]]) {
    float3 c = tex.sample(s, in.uv).rgb;
    float2 d = in.uv - float2(0.5);
    float vig = 1.0 - smoothstep(0.3, 0.7, length(d));
    return float4(c * vig, 1.0);
}
)MSL";

class MetalRenderContext;

class MetalShaderProgram : public ShaderProgram {
public:
    MetalShaderProgram(MetalRenderContext* ctx, const std::string& fsName);
    ~MetalShaderProgram() override;

    void use() override;
    void setUniform(const char*, float) override {}        // Phase 0: no uniforms used
    void setUniform(const char*, float, float) override {}
    void setUniform(const char*, float, float, float) override {}
    void setUniform(const char*, float, float, float, float) override {}
    void setUniform(const char*, int) override {}
    void setUniformMatrix4(const char*, const float*) override {}
    void bindTexture(const char*, const TextureHandle& tex, int unit) override;

    id<MTLRenderPipelineState> pipeline() const {
        return (__bridge id<MTLRenderPipelineState>)pipelineState_;
    }

private:
    MetalRenderContext* ctx_;
    void* pipelineState_ = nullptr;
    void* boundTexture_ = nullptr;
};

// -----------------------------------------------------------------------------
// MetalRenderContext
// -----------------------------------------------------------------------------
class MetalRenderContext : public RenderContext {
public:
    explicit MetalRenderContext(id<MTLDevice> device) : device_(device) {
        commandQueue_ = [device newCommandQueue];

        NSError* err = nil;
        library_ = [device newLibraryWithSource:@(kMslLibrarySource)
                                        options:nil error:&err];
        if (!library_) {
            os_log_error(OS_LOG_DEFAULT, "Metal library compile failed: %{public}@", err);
        }

        // Default sampler
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = MTLSamplerMinMagFilterLinear;
        sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler_ = [device newSamplerStateWithDescriptor:sd];

        // Fullscreen quad VBO
        static const float kQuad[] = {
            -1.f,-1.f,  0.f, 1.f,   // flipped V to match top-left UV
             1.f,-1.f,  1.f, 1.f,
            -1.f, 1.f,  0.f, 0.f,
             1.f, 1.f,  1.f, 0.f,
        };
        quadBuffer_ = [device newBufferWithBytes:kQuad length:sizeof(kQuad)
                                         options:MTLResourceStorageModeShared];
    }

    std::unique_ptr<TextureHandle> createTexture(int w, int h,
                                                 TextureHandle::Format format) override {
        MTLPixelFormat pf = (format == TextureHandle::Format::R8)
                            ? MTLPixelFormatR8Unorm : MTLPixelFormatBGRA8Unorm;
        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:pf width:w height:h mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        id<MTLTexture> t = [device_ newTextureWithDescriptor:desc];
        return std::make_unique<TextureHandle>(
            reinterpret_cast<uint64_t>((__bridge_retained void*)t),
            w, h, format, TextureHandle::Ownership::Owned);
    }

    std::unique_ptr<Framebuffer> createFramebuffer(int w, int h,
                                                   TextureHandle::Format format) override {
        return std::make_unique<MetalFramebuffer>(device_, w, h, format);
    }

    std::unique_ptr<ShaderProgram> createShader(const std::string& /*vs*/,
                                                const std::string& fsSource) override {
        // Phase 0: map known fragment shader content to MSL function name
        std::string fname = "passthrough_fs";
        if (fsSource.find("grayscale") != std::string::npos)
            fname = "grayscale_fs";
        else if (fsSource.find("invert") != std::string::npos ||
                 fsSource.find("1.0 - c") != std::string::npos)
            fname = "invert_fs";
        else if (fsSource.find("vignette") != std::string::npos ||
                 fsSource.find("vig") != std::string::npos)
            fname = "vignette_fs";
        return std::make_unique<MetalShaderProgram>(this, fname);
    }

    void bindFramebuffer(Framebuffer* fbo) override { currentFbo_ = fbo; }

    void clearColor(float r, float g, float b, float a) override {
        clearColor_ = MTLClearColorMake(r, g, b, a);
    }

    void drawFullscreenQuad(ShaderProgram* prog) override {
        auto* metalFbo = static_cast<MetalFramebuffer*>(currentFbo_);
        if (!metalFbo) return;

        MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = metalFbo->mtlTexture();
        rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpd.colorAttachments[0].clearColor = clearColor_;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLCommandBuffer> cb = [commandQueue_ commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpd];

        auto* msp = static_cast<MetalShaderProgram*>(prog);
        [enc setRenderPipelineState:msp->pipeline()];
        [enc setVertexBuffer:quadBuffer_ offset:0 atIndex:0];

        // Bind the most recently set input texture (msp manages this)
        msp->use();   // applies its bound texture/sampler to a stored state we read below
        if (lastBoundTexture_) {
            [enc setFragmentTexture:lastBoundTexture_ atIndex:0];
        }
        [enc setFragmentSamplerState:sampler_ atIndex:0];

        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [enc endEncoding];
        [cb commit];
    }

    void blit(const TextureHandle&, Framebuffer*) override {}
    void flush() override {}
    void waitGpu() override {}

    const char* backendName() const override { return "Metal"; }
    int maxTextureSize() const override { return 16384; }

    // Helpers accessed by MetalShaderProgram
    id<MTLDevice>    device()    const { return device_; }
    id<MTLLibrary>   library()   const { return library_; }

    void setLastBoundTexture(id<MTLTexture> t) { lastBoundTexture_ = t; }

private:
    id<MTLDevice>          device_;
    id<MTLCommandQueue>    commandQueue_;
    id<MTLLibrary>         library_;
    id<MTLSamplerState>    sampler_;
    id<MTLBuffer>          quadBuffer_;
    id<MTLTexture>         lastBoundTexture_ = nil;

    Framebuffer*  currentFbo_ = nullptr;
    MTLClearColor clearColor_ = MTLClearColorMake(0,0,0,1);
};

// -----------------------------------------------------------------------------
// MetalShaderProgram out-of-line methods
// -----------------------------------------------------------------------------
MetalShaderProgram::MetalShaderProgram(MetalRenderContext* ctx, const std::string& fsName)
    : ctx_(ctx) {
    id<MTLFunction> vs = [ctx->library() newFunctionWithName:@"passthrough_vs"];
    id<MTLFunction> fs = [ctx->library() newFunctionWithName:
                            [NSString stringWithUTF8String:fsName.c_str()]];

    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = vs;
    pd.fragmentFunction = fs;
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    MTLVertexDescriptor* vd = [MTLVertexDescriptor new];
    vd.attributes[0].format = MTLVertexFormatFloat2;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat2;
    vd.attributes[1].offset = 8;
    vd.attributes[1].bufferIndex = 0;
    vd.layouts[0].stride = 16;
    pd.vertexDescriptor = vd;

    NSError* err = nil;
    id<MTLRenderPipelineState> pso =
        [ctx->device() newRenderPipelineStateWithDescriptor:pd error:&err];
    pipelineState_ = (__bridge_retained void*)pso;
}

MetalShaderProgram::~MetalShaderProgram() {
    if (pipelineState_) {
        id<MTLRenderPipelineState> pso =
            (__bridge_transfer id<MTLRenderPipelineState>)pipelineState_;
        (void)pso;
    }
}

void MetalShaderProgram::use() {
    if (boundTexture_) {
        ctx_->setLastBoundTexture((__bridge id<MTLTexture>)boundTexture_);
    }
}

void MetalShaderProgram::bindTexture(const char*, const TextureHandle& tex, int) {
    boundTexture_ = reinterpret_cast<void*>(tex.nativeHandle());
}

// -----------------------------------------------------------------------------
// Factory
// -----------------------------------------------------------------------------
std::unique_ptr<RenderContext> createMetalRenderContext(void* mtlDevice) {
    id<MTLDevice> dev = (__bridge id<MTLDevice>)mtlDevice;
    return std::make_unique<MetalRenderContext>(dev);
}

}  // namespace community_ar

#endif  // __APPLE__
