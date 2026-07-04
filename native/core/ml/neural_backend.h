// neural_backend.h
// =============================================================================
// Community AR — Abstract neural inference backend
//
// Updated for production GPU texture binding. The setInputTexture() contract
// now requires the model to consume the texture WITHOUT CPU readback. If a
// backend cannot bind a particular texture format (e.g. an OES external
// texture on an older TFLite version), it must fall back to an internal
// blit-to-2D step rather than returning a CPU-readback path silently.
// =============================================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace community_ar {

class TextureHandle;
class RenderContext;

// -----------------------------------------------------------------------------
// Tensor metadata (unchanged from Phase 1)
// -----------------------------------------------------------------------------
enum class TensorType { Float32, Uint8, Int32 };

struct TensorShape {
    int dims[4] = {0, 0, 0, 0};
    int rank = 0;
    int totalElements() const {
        int n = 1; for (int i = 0; i < rank; ++i) n *= dims[i]; return n;
    }
};

struct TensorSpec {
    std::string name;
    TensorType  type;
    TensorShape shape;
};

// -----------------------------------------------------------------------------
// InputBindingMode — explicit contract about how a model receives its image
//
// `GpuTextureZeroCopy` is the production path. If a backend cannot honor it
// for a given model, it must say so via `actualBindingMode()`.
// -----------------------------------------------------------------------------
enum class InputBindingMode {
    GpuTextureZeroCopy,  // The model reads the GPU texture directly
    GpuTextureBlit,      // Backend blits texture to its own GPU buffer (still GPU-only, ~0.2ms)
    CpuUpload            // Last resort: CPU readback + re-upload. Avoid.
};

// -----------------------------------------------------------------------------
// CameraInputRect — region within the camera texture to feed to the model.
//
// All coords in pixels, top-left origin. The backend handles rotation and
// resizing to the model's expected input shape.
// -----------------------------------------------------------------------------
struct CameraInputRect {
    int x = 0, y = 0;        // top-left in source texture pixels
    int width = 0, height = 0;
    int rotationDeg = 0;     // 0, 90, 180, 270
    bool mirrorX = false;    // for front-facing camera
    // Input value range the model was trained on: false → [0, 1] (FaceMesh,
    // Iris, segmenters), true → [-1, 1] (MediaPipe face_detection). Applies to
    // float input tensors on the CPU-staged path; the GL-binding path always
    // samples [0, 1], so signed models are only correct via CPU staging.
    bool signedInput = false;
};

// -----------------------------------------------------------------------------
// NeuralModel — loaded model ready for inference
// -----------------------------------------------------------------------------
class NeuralModel {
public:
    virtual ~NeuralModel() = default;

    virtual const std::vector<TensorSpec>& inputs() const = 0;
    virtual const std::vector<TensorSpec>& outputs() const = 0;

    // -- Input binding --
    // Returns the actual binding mode used. Models authored against this
    // interface assume zero-copy; the return value indicates whether that
    // assumption held. The render thread can use this for profiling.
    virtual InputBindingMode setInputTexture(int inputIndex,
                                             const TextureHandle& tex,
                                             const CameraInputRect& srcRect) = 0;

    // CPU input for the rare non-image case
    virtual void setInputData(int inputIndex, const void* data, size_t bytes) = 0;

    // -- Execution --
    virtual bool run() = 0;

    // -- Output access --
    virtual bool readOutput(int outputIndex, void* dstBuffer, size_t bytes) = 0;

    // For models with image outputs (segmentation), bind to an existing GPU
    // texture rather than reading back. Returns false if the model cannot
    // produce that output as a texture.
    virtual bool bindOutputTexture(int outputIndex, TextureHandle* tex) = 0;
};

// -----------------------------------------------------------------------------
// NeuralBackend
// -----------------------------------------------------------------------------
struct BackendConfig {
    bool preferGpu = true;
    bool preferNpu = true;
    int  numCpuThreads = 2;
    std::string modelDirectory;

    // Required for any GPU texture binding to work. The backend captures
    // the platform GPU handles from the render context.
    RenderContext* renderContext = nullptr;
};

class NeuralBackend {
public:
    virtual ~NeuralBackend() = default;

    virtual std::unique_ptr<NeuralModel> loadModel(const std::string& name) = 0;
    virtual const char* activeAcceleratorName() const = 0;

    // Diagnostic: tells whether the backend successfully wired up the
    // zero-copy GPU path for the given model. Used by stats.
    virtual InputBindingMode preferredBindingMode(NeuralModel* m) const = 0;
};

#if defined(__ANDROID__)
std::unique_ptr<NeuralBackend> createTfliteBackend(const BackendConfig& cfg);
#endif
#if defined(__APPLE__)
std::unique_ptr<NeuralBackend> createCoreMLBackend(const BackendConfig& cfg);
#endif

}  // namespace community_ar
