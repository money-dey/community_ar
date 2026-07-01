// gles_compute.cpp
// =============================================================================
// OpenGL ES 3.1 compute primitives implementation (Android).
//
// Three things stitched together:
//   1. SSBO creation with GL_DYNAMIC_DRAW (uploads) and GL_DYNAMIC_READ
//      (CPU-visible readback buffers).
//   2. Compute program compile + link.
//   3. Async readback via glMapBufferRange + glFenceSync. We map the SSBO
//      range as soon as the dispatch is issued, then poll the fence each
//      frame. The first poll where glClientWaitSync returns
//      GL_ALREADY_SIGNALED or GL_CONDITION_SATISFIED means the data is
//      ready to read.
//
// All three combined give us a no-stall path: dispatch → fence → wait via
// polling → read. Worst-case latency is ~1-2 frames (33-66ms at 30fps),
// which is invisible for slow-changing data like skin tone.
// =============================================================================

#if defined(__ANDROID__)

#include "compute_primitives.h"
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>   // GL_TEXTURE_EXTERNAL_OES (OES_EGL_image_external)
#include <android/log.h>
#include <cstring>

#define LOG_TAG "CommunityAR-Compute"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

namespace community_ar {

// -----------------------------------------------------------------------------
// GlesShaderStorageBuffer
// -----------------------------------------------------------------------------
class GlesShaderStorageBuffer : public ShaderStorageBuffer {
public:
    GlesShaderStorageBuffer(size_t bytes, StorageMode mode, const void* initial)
        : sizeBytes_(bytes), mode_(mode) {
        glGenBuffers(1, &name_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, name_);
        // For readback we want PERSISTENT + COHERENT mapping where supported
        // (GLES 3.1 doesn't have glBufferStorage; on platforms with the
        // EXT_buffer_storage extension we'd use that. For broad compatibility
        // we use plain glBufferData and read via glMapBufferRange.)
        GLenum usage = (mode == StorageMode::Shared)
                       ? GL_DYNAMIC_READ : GL_DYNAMIC_DRAW;
        glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, initial, usage);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    ~GlesShaderStorageBuffer() override {
        if (name_) glDeleteBuffers(1, &name_);
    }

    uint64_t nativeHandle() const override { return name_; }
    size_t   sizeBytes()    const override { return sizeBytes_; }
    StorageMode storageMode() const override { return mode_; }

private:
    GLuint name_ = 0;
    size_t sizeBytes_;
    StorageMode mode_;
};

// -----------------------------------------------------------------------------
// GlesComputeProgram
// -----------------------------------------------------------------------------
class GlesComputeProgram : public ComputeProgram {
public:
    explicit GlesComputeProgram(const std::string& src) {
        GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
        const char* p = src.c_str();
        glShaderSource(shader, 1, &p, nullptr);
        glCompileShader(shader);
        GLint ok;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            LOGE("Compute shader compile failed: %s\n--- Source ---\n%s",
                 log, src.c_str());
            glDeleteShader(shader);
            return;
        }
        program_ = glCreateProgram();
        glAttachShader(program_, shader);
        glLinkProgram(program_);
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
            LOGE("Compute program link failed: %s", log);
        }
        glDeleteShader(shader);
    }

    ~GlesComputeProgram() override {
        if (program_) glDeleteProgram(program_);
    }

    void use() override { glUseProgram(program_); }

    void bindStorageBuffer(int bindingPoint, ShaderStorageBuffer* buf) override {
        if (!buf) return;
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint,
                         static_cast<GLuint>(buf->nativeHandle()));
    }

    void bindTexture(const char* sampler, const TextureHandle& tex, int unit) override {
        glActiveTexture(GL_TEXTURE0 + unit);
        GLenum target = (tex.format() == TextureHandle::Format::ExternalOES)
                        ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;
        glBindTexture(target, static_cast<GLuint>(tex.nativeHandle()));
        GLint loc = glGetUniformLocation(program_, sampler);
        if (loc >= 0) glUniform1i(loc, unit);
    }

    void setUniform(const char* n, float v) override {
        GLint loc = glGetUniformLocation(program_, n);
        if (loc >= 0) glUniform1f(loc, v);
    }
    void setUniform(const char* n, int v) override {
        GLint loc = glGetUniformLocation(program_, n);
        if (loc >= 0) glUniform1i(loc, v);
    }
    void setUniform(const char* n, float x, float y) override {
        GLint loc = glGetUniformLocation(program_, n);
        if (loc >= 0) glUniform2f(loc, x, y);
    }

    GLuint nativeProgram() const { return program_; }

private:
    GLuint program_ = 0;
};

// -----------------------------------------------------------------------------
// GlesAsyncReadback
//
// Uses a sync fence (glFenceSync) to know when the GPU has finished writing
// the buffer. CPU-side, we glMapBufferRange the SSBO so reads cost nothing
// once the fence is signaled.
// -----------------------------------------------------------------------------
class GlesAsyncReadback : public AsyncReadback {
public:
    GlesAsyncReadback(GLuint srcBuffer, size_t bytes)
        : bytes_(bytes), srcBuffer_(srcBuffer) {
        fence_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }

    ~GlesAsyncReadback() override { release(); }

    bool isReady() const override {
        if (resolved_) return true;
        if (!fence_) return false;
        GLenum r = glClientWaitSync(fence_, 0, 0);  // non-blocking poll
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED) {
            // Map the buffer once we know writes are visible
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, srcBuffer_);
            mapped_ = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes_,
                                       GL_MAP_READ_BIT);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            resolved_ = (mapped_ != nullptr);
            return resolved_;
        }
        return false;
    }

    const void* data() const override { return mapped_; }
    size_t      size() const override { return bytes_; }

    void release() override {
        if (mapped_) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, srcBuffer_);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            mapped_ = nullptr;
        }
        if (fence_) {
            glDeleteSync(fence_);
            fence_ = 0;
        }
    }

private:
    size_t bytes_;
    GLuint srcBuffer_ = 0;
    GLsync fence_ = 0;
    // Mapped lazily inside the const isReady() once the fence signals, so it
    // (like resolved_) must be mutable.
    mutable void* mapped_ = nullptr;
    mutable bool resolved_ = false;
};

// -----------------------------------------------------------------------------
// RenderContext compute method implementations.
//
// In the production codebase these would be members of GlesRenderContext.
// Phase 1 fixes keep them in this file so the delta is reviewable. The
// signatures mirror RenderContextCompute exactly.
// -----------------------------------------------------------------------------
namespace gles_compute_impl {

std::unique_ptr<ShaderStorageBuffer> createStorageBuffer(
        size_t bytes, ShaderStorageBuffer::StorageMode mode,
        const void* initial) {
    return std::make_unique<GlesShaderStorageBuffer>(bytes, mode, initial);
}

std::unique_ptr<ComputeProgram> createComputeProgram(const std::string& src) {
    return std::make_unique<GlesComputeProgram>(src);
}

void dispatchCompute(ComputeProgram* program, int gx, int gy, int gz) {
    if (!program) return;
    program->use();
    glDispatchCompute(gx, gy, gz);
}

void storageBufferBarrier() {
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_BUFFER_UPDATE_BARRIER_BIT);
}

std::unique_ptr<AsyncReadback> requestReadback(ShaderStorageBuffer* src, size_t bytes) {
    if (!src) return nullptr;
    return std::make_unique<GlesAsyncReadback>(
        static_cast<GLuint>(src->nativeHandle()), bytes);
}

}  // namespace gles_compute_impl

}  // namespace community_ar

#endif  // __ANDROID__
