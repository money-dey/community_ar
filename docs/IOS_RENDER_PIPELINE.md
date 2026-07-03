# iOS Render Pipeline — Design & Bring-Up Guide

The iOS Metal pipeline that should take a camera frame all the way to a pixel on
screen is, like the Android one before PR #22, a **non-functional sketch**: it
compiles and the plumbing is *shaped* right, but the native render output never
actually reaches the texture Flutter displays, so the preview would be blank.
This document explains exactly what's missing, gives a target architecture that
matches the ownership decision in
[`RENDER_PIPELINE_OWNERSHIP.md`](RENDER_PIPELINE_OWNERSHIP.md) (Option A — the
platform owns the GPU handles), and lists the implementation + verification
steps so a Metal-focused contributor can drive it on a Mac.

Scope: **Phase 0 display** — getting the live camera (and the four test shaders)
to appear in the Flutter `Texture` widget on iOS. Effects/perception wiring is a
larger, separate effort; nothing renders until this works first.

> Status: **not implemented.** The iOS side has never been built or run in this
> project's dev environment (no macOS/Xcode — see `START_HERE.md`), so unlike the
> Android pipeline this is design-only and has had **zero** compile verification.
> Written 2026-07-03. Treat code snippets as direction, not tested source.

---

## 1. The intended flow (and why it doesn't work today)

Intended, and mostly present in `ios/Classes/`:

1. `CommunityARPlugin.createSession()` makes an `MTLDevice`
   (`MTLCreateSystemDefaultDevice()`), passes it to native as `cfg.gpuContext`,
   and registers a `CommunityAROutputTexture` (a `FlutterTexture`) with the
   `FlutterTextureRegistry`.
2. `CameraStream` runs `AVCaptureSession` → `CVPixelBuffer` (BGRA8,
   IOSurface-backed) → `CVMetalTextureCache` → `MTLTexture`, zero-copy, and calls
   back per frame.
3. The callback invokes `car_p0_submit_frame(...)` with the camera `MTLTexture`;
   native `MetalRenderContext` renders it through a test shader into a
   `MetalFramebuffer`.
4. Flutter pulls frames: it calls `CommunityAROutputTexture.copyPixelBuffer()`,
   which should return the rendered image as a `CVPixelBuffer`.

### The defects (all real, all blocking)

1. **The render output is never connected to what Flutter reads.**
   `MetalRenderContext` renders into a `MetalFramebuffer` whose `MTLTexture` is
   allocated with `MTLStorageModePrivate` (see `metal_render_context.mm`).
   Meanwhile `copyPixelBuffer()` (in `CommunityARPlugin.swift`) creates a **fresh,
   blank** `CVPixelBuffer` with `CVPixelBufferCreate` and returns it — it never
   reads `car_p0_get_output_texture()` and never copies/binds the native render
   target into that buffer. So Flutter always displays an empty buffer. The code
   comment even admits the real design ("allocate the texture from
   `CVPixelBufferPool` upfront and pass the underlying `MTLTexture` to native as
   the render target"). **This is the primary black-screen bug.**

2. **Private storage can't be shared with Flutter.** A `CVPixelBuffer` Flutter can
   composite must be **IOSurface-backed** and Metal-compatible. A
   `MTLStorageModePrivate` texture has no IOSurface and can't be wrapped in a
   `CVPixelBuffer`. The display render target must be created *from* a
   `CVPixelBuffer` (via `CVMetalTextureCacheCreateTextureFromImage`), not as a
   standalone private texture.

3. **No GPU/CPU synchronization.** `drawFullscreenQuad` commits a command buffer;
   `flush()` and `waitGpu()` are no-ops. Swift calls `notifier()` /
   `textureFrameAvailable` *immediately* after `car_p0_submit_frame` returns —
   but the GPU work is asynchronous, so Flutter can sample the target mid-render
   or before it. There's no `addCompletedHandler`, no fence, no double-buffering.

4. **Camera texture lifetime hazard.** The camera `MTLTexture` is passed to native
   via `Unmanaged.passUnretained`. Its backing `CVMetalTexture` / `CVPixelBuffer`
   must stay alive until the GPU finishes sampling it. With
   `alwaysDiscardsLateVideoFrames = true` and nothing retaining the buffer across
   the async render, the frame can be recycled mid-draw.

5. **Wrong target model.** iOS is **pull-based** (Flutter asks for a pixel buffer
   when *it* wants one), so there is no `eglSwapBuffers` equivalent and no
   "default framebuffer." The correct model is: native renders into the
   **IOSurface-backed texture that Flutter will read**, not into a private
   offscreen texture. So the Android fix (render into fbo 0 + swap) does **not**
   translate directly; the iOS fix is "make the render target *be* the Flutter
   pixel buffer's texture."

### Latent (not Phase-0-blocking, but note them)

- **Shared bound-texture state.** `MetalRenderContext` stores `lastBoundTexture_`
  on the context and reads it in `drawFullscreenQuad`, rather than per-encoder.
  Fine for one shader per frame; a hazard once multiple effects encode in a
  frame. Revisit when effects land.
- **Shader source matching by string search.** `createShader` picks an MSL
  function by substring-matching the GLSL source. Acceptable for the four fixed
  Phase 0 shaders; replaced by SPIRV-Cross transpilation in Phase 5.

---

## 2. Target architecture (Option A — Swift owns Metal)

Per [`RENDER_PIPELINE_OWNERSHIP.md`](RENDER_PIPELINE_OWNERSHIP.md), the platform
layer owns the GPU handles and the presentation surface; C++ owns the render
engine. On iOS this is the *only* natural fit — the `MTLDevice`, the
`FlutterTexture` protocol, and `AVCaptureSession` are all Swift-side.

The reliable model for "Metal-processed camera into a Flutter texture" is:

> **Swift owns a pool of IOSurface-backed `CVPixelBuffer`s. Each camera frame,
> Swift wraps the current pool buffer as an `MTLTexture` and hands it to native
> as the render *target*; native renders camera → that texture; Swift returns
> that same `CVPixelBuffer` from `copyPixelBuffer()` once the GPU signals done.**

This mirrors Android's "platform owns the presentation surface," differing only
in push (`eglSwapBuffers`) vs. pull (`copyPixelBuffer`).

### Where the render target comes from — two sub-options

- **A1 (recommended): Swift owns the target, native renders into it.**
  Swift keeps a `CVPixelBufferPool`, wraps each buffer via `CVMetalTextureCache`
  into an `MTLTexture`, and passes that texture handle to native every frame as
  the output. Native renders the camera texture into the supplied target.
  *Pros:* keeps all `CVPixelBuffer`/IOSurface/Flutter lifetime in Swift (where
  those types live), symmetric with the Android display call, minimal native
  change. *Cons:* one extra ABI parameter (the output texture handle).

- **A2: native owns an IOSurface-backed target, exposes its IOSurface to Swift.**
  Native allocates the render texture from an `IOSurface` and hands the surface
  back for Swift to wrap in a `CVPixelBuffer`. *Cons:* pushes IOSurface/CVPixel
  lifetime into C++/ObjC++, more native surface, less symmetric with Android.

**Recommendation: A1.** It matches the ownership decision, keeps the fiddly
buffer-lifetime code in Swift, and parallels the Android
`car_p0_submit_frame_display` shape.

---

## 3. Implementation plan (Option A1)

### 3.1 A pool of display targets (Swift)
In `CommunityAROutputTexture` (or a small `MetalDisplayTarget` helper):
- Create a `CVMetalTextureCache` for the device.
- Create a `CVPixelBufferPool` of `kCVPixelFormatType_32BGRA` buffers at the
  display size, with `kCVPixelBufferMetalCompatibilityKey: true` and
  `kCVPixelBufferIOSurfacePropertiesKey: [:]`.
- Keep a small ring (2–3) of `(CVPixelBuffer, CVMetalTexture, MTLTexture)` tuples
  so Flutter can read frame N while frame N+1 renders.

### 3.2 Per-frame loop (Swift → native → completion)
On each `CameraStream` callback (already delivering the camera `MTLTexture`):
1. Dequeue/pick the next pool buffer; get its `MTLTexture` (the render *target*).
2. Call native to render **camera texture → target texture** (new ABI, §3.4).
   Pass both texture handles + dimensions.
3. In the command buffer's `addCompletedHandler`, publish that buffer as the
   "latest ready" and call `textureRegistry.textureFrameAvailable(id)`.
4. `copyPixelBuffer()` returns the "latest ready" `CVPixelBuffer`
   (`passRetained`), *not* a freshly-created blank one.

Retain the camera frame's `CVPixelBuffer`/`CVMetalTexture` until the completion
handler fires (fixes defect #4).

### 3.3 Native changes (small)
- Add a render path that draws into a **caller-supplied** output `MTLTexture`
  instead of an internally-created `MetalFramebuffer`. Concretely, a
  `MetalFramebuffer` variant (or an overload) that *adopts* an external
  `id<MTLTexture>` as its color attachment, so `drawFullscreenQuad`'s
  `renderPassDescriptor` points at the Flutter-visible texture.
- Add `addCompletedHandler` (or return after `waitUntilCompleted` only if you
  must — prefer the async handler) so Swift can gate `textureFrameAvailable`.
- Keep the camera texture bound as `texture2d<float>` (it already arrives as a
  normal `MTLTexture` from `CVMetalTextureCache`, **not** an external-OES analog —
  so, unlike Android, no special sampler type is needed).

### 3.4 C ABI (new symbol — do not modify existing)
Per CLAUDE.md §2, add a **new** symbol rather than changing `car_p0_submit_frame`.
Because iOS renders into a supplied target and needs no UV matrix (orientation is
handled by AVFoundation, §4), the natural signature is:

```c
// Render cameraTextureHandle into a caller-supplied output texture
// (an IOSurface-backed id<MTLTexture> Flutter will read). iOS pull model:
// the platform presents by returning the backing CVPixelBuffer, so there is
// no swap. width/height are the display dimensions.
CAR_EXPORT CARStatus car_p0_submit_frame_to_target(CARSession* session,
                                                   uint64_t cameraTextureHandle,
                                                   uint64_t outputTextureHandle,
                                                   int width, int height);
```

(If a future need for a UV transform appears, mirror the Android
`texMatrix` parameter. Android's `car_p0_submit_frame_display` and this iOS
symbol are cousins: both "render into a platform-owned target," specialized to
push vs. pull.)

### 3.5 Touch points summary
| File | Change |
|---|---|
| `CommunityARPlugin.swift` | `copyPixelBuffer` returns the rendered pool buffer; drive the pool + completion-gated `textureFrameAvailable`; call the new ABI |
| `CameraStream.swift` | retain the camera `CVPixelBuffer`/`CVMetalTexture` until render completes |
| `metal_render_context.mm` | render into an adopted external `MTLTexture`; add a completion handler; make the target IOSurface-friendly |
| `community_ar_phase0_api.h/.cpp` | add `car_p0_submit_frame_to_target` (new symbol) |
| `phase0_session.{h,cpp}` | `submitFrameToTarget` that binds the supplied texture as the render target |

---

## 4. Sizing, rotation, mirroring
Simpler than Android: `CameraStream.swift` already sets
`connection.videoOrientation = .portrait` and `connection.isVideoMirrored =
isFront`, so **AVFoundation delivers upright, correctly-mirrored frames** and the
callback reports `rotation = 0`. No UV-transform matrix is required for Phase 0.
- Display size = the camera preset (`.hd1280x720`) in the delivered orientation
  (portrait → 720×1280). Match the pool buffer size + `getOutputDimensions`.
- *Tradeoff:* AVFoundation-applied rotation may cost an internal copy vs. baking
  it into UVs; acceptable for Phase 0. Revisit only if profiling flags it.

---

## 5. Synchronization & buffering
- **Triple-buffer** the display pool so Flutter can read while the GPU renders the
  next frame; never hand Flutter a buffer the GPU is still writing.
- Gate `textureFrameAvailable` on the command buffer's `addCompletedHandler`
  (defect #3), not on `submit_frame` returning.
- Retain each in-flight camera + target buffer until its completion handler fires
  (defect #4). A small in-flight semaphore (e.g. `DispatchSemaphore(value: 3)`)
  bounds queue depth.

---

## 6. On-device verification checklist
Requires a Mac + Xcode + a physical iPhone (Metal on the Simulator is unreliable
for camera/IOSurface work). **None of this is verifiable in the current dev
environment.** Check in order, watching Xcode console + the Metal debugger /
GPU frame capture:

1. **Device + caches create:** `MTLCreateSystemDefaultDevice`,
   `CVMetalTextureCacheCreate`, `CVPixelBufferPoolCreate` all succeed.
2. **Camera frames arrive:** `captureOutput` fires ~30/s; camera `MTLTexture`
   is non-nil.
3. **Render encodes:** a GPU frame capture shows the fullscreen-quad draw into
   the target texture; no validation-layer errors.
4. **Completion fires:** `addCompletedHandler` runs; `textureFrameAvailable` is
   called after it.
5. **Widget shows something:** first target is `CAR_TEST_MODE_PASSTHROUGH`
   showing live camera; then grayscale/invert/vignette to prove the shader path.
6. **Orientation/mirror:** upright + correct handedness on front and back cameras.

Common failure modes: returning a blank/fresh `CVPixelBuffer` (defect #1);
private-storage target (defect #2); tearing from missing synchronization
(defect #3); flickering/garbage from a recycled camera buffer (defect #4);
Simulator Metal quirks (test on device).

---

## 7. After Phase 0 display works
Same order as Android (see `CONSOLIDATION_AND_BRINGUP.md`):
1. Phase 2/3 method-channel handlers + FFI so effects install.
2. Wire the render loop to `renderFramePhase2()` so the effect graph + perception
   run per frame.
3. Core ML models (`tools/convert_models_to_coreml.py`) + model directory
   plumbed into `BackendConfig`.

Each is its own device-iteration loop.

---

## 8. Relationship to the Android pipeline
Android (PR #22) and this iOS design are the **same ownership pattern** (Option
A) expressed through different platform primitives — see the mapping table in
[`RENDER_PIPELINE_OWNERSHIP.md`](RENDER_PIPELINE_OWNERSHIP.md) §8. The key
divergences a contributor must keep in mind:
- **Push vs. pull:** Android presents with `eglSwapBuffers`; iOS presents by
  returning a `CVPixelBuffer` when Flutter pulls. iOS therefore needs no window
  surface and no swap, but *does* need the render target to be the exact texture
  Flutter reads.
- **Camera sampler:** Android's camera is an external-OES texture needing
  `samplerExternalOES` + a UV transform; iOS's is an ordinary `MTLTexture` with
  orientation pre-applied by AVFoundation.
- **Threading:** Android has one context-affine GL thread; Metal command buffers
  are largely thread-agnostic, so iOS's constraint is *buffer lifetime +
  completion ordering*, not thread affinity.
