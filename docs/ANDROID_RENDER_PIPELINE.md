# Android Render Pipeline — Design & Bring-Up Guide

The Android GL/EGL pipeline that should take a camera frame all the way to a
pixel on screen is currently a **non-functional sketch**. This document explains
exactly what's missing, gives a concrete target architecture, and lists the
implementation + on-device verification steps so a graphics-focused contributor
can drive it.

Scope: **Phase 0 display** — getting the live camera (and the four test shaders)
to actually appear in the `Texture` widget. Effects/perception wiring is a
larger, separate effort (see the end + `CONSOLIDATION_AND_BRINGUP.md`); nothing
renders until this pipeline works first.

> Status: as of PR #17 the app builds, installs, and requests camera
> permission, but the preview is black because none of the below is
> implemented. Written 2026-07-03.

---

## 1. The intended flow (and why it doesn't work today)

Intended: camera → OES texture → native GL (test shader / effects) → Flutter
texture → screen. Concretely the code tries:

1. `CommunityARPlugin.createSession()` allocates a Flutter
   `SurfaceTextureEntry` and calls `nativeCreateSession(entry.surfaceTexture())`.
2. `CameraStream.start()` creates an OES texture, wraps it in a `SurfaceTexture`,
   and points Camera2 at it; each frame calls back `nativeSubmitFrame(oesTex,…)`.
3. Native `Phase0Session::processFrame()` renders the OES texture through a test
   shader into an offscreen framebuffer `outputFbo_`.
4. Flutter displays `Texture(textureId: surfaceEntry.id())`.

### The defects (all real, all blocking)

1. **No EGL context anywhere.** `CameraStream.createOesTexture()` calls
   `glGenTextures` on the camera `HandlerThread` with **no current EGL context**
   (its own comment says "the host plugin should set one up first" — nothing
   does). GL calls with no current context are no-ops/errors, so the camera OES
   texture is never validly created.
2. **No window surface / no presentation.** `nativeCreateSession` **ignores**
   the Flutter `SurfaceTexture` (the JNI param is commented out); it only
   captures whatever `eglGetCurrentContext()` happened to be. Native renders into
   `outputFbo_` and **never presents into the Flutter `SurfaceTexture`**, which is
   what the `Texture` widget shows. So the widget is always blank.
3. **Cross-context texture handle.** The OES texture name is passed from the
   camera thread to native, but native's context (if it had one) is different and
   **not in a shared group**, so a raw GL name isn't valid there.
4. **Wrong sampler type.** The camera texture is `GL_TEXTURE_EXTERNAL_OES`, but
   the native test shaders sample `uniform sampler2D uTex`. Sampling an external
   texture needs `#extension GL_OES_EGL_image_external : require` +
   `samplerExternalOES`. (The compute path handles the OES target; the
   full-screen render path does not.)
5. **No `SurfaceTexture` transform applied.** `SurfaceTexture.getTransformMatrix`
   must be applied to the UVs or the image is cropped/flipped/misrotated.

Net: the whole "context + threading + texture-sharing + presentation" model was
never built. This is a from-scratch GL integration, not a one-line handoff.

---

## 2. Target architecture

The reliable, well-trodden model for "GL-processed camera into a Flutter texture"
is: **one render thread owns one EGL context; the camera OES texture and an
`EGLWindowSurface` (created from the Flutter `SurfaceTexture`) both live on it;
each frame renders and `eglSwapBuffers` presents into the Flutter texture.**

Two places the EGL can live:

### Option A — Kotlin owns EGL (recommended)
Create the `EGLContext`, camera OES texture, and window surface on one Kotlin
render thread with `EGL14`. Native renders on that same thread into the default
framebuffer (fbo 0 == the window surface); Kotlin (or native) swaps.

- **Pros:** standard Flutter-plugin pattern; debuggable with Android/GPU tooling;
  EGL in one place; the native side barely changes (just render to fbo 0).
- **Cons:** the camera + surface plumbing is in Kotlin, slightly away from the
  C++ engine.

### Option B — Native (C++) owns EGL
Native creates the context + window surface via the EGL C API and owns the render
thread; Kotlin hands over the Flutter `Surface` and wires Camera2 to a
native-created surface.

- **Pros:** rendering sits next to the C++ engine.
- **Cons:** more JNI surface; all EGL lives in C++ that can't be exercised with
  Android GPU tools; harder to debug.

**Recommendation: Option A.** It minimizes native change, centralizes EGL where
it's most debuggable, and is the pattern most Flutter camera/GL plugins use.

---

## 3. Implementation plan (Option A)

### 3.1 One render thread + EGL context (Kotlin)
Create a `HandlerThread` "CommunityAR-GL". On it, with `EGL14`:
- `eglGetDisplay(EGL_DEFAULT_DISPLAY)`, `eglInitialize`.
- `eglChooseConfig` with `EGL_RENDERABLE_TYPE = EGL_OPENGL_ES2_BIT`,
  `EGL_SURFACE_TYPE = EGL_WINDOW_BIT`, RGBA8.
- `eglCreateContext` with `EGL_CONTEXT_CLIENT_VERSION = 3` (ES 3.x), no sharing
  needed.
- Keep all subsequent GL/native calls on this thread.

### 3.2 Window surface from the Flutter SurfaceTexture (Kotlin)
- `entry.surfaceTexture().setDefaultBufferSize(w, h)` (the display resolution —
  see §4 sizing).
- `val flutterSurface = Surface(entry.surfaceTexture())`.
- `windowSurface = eglCreateWindowSurface(display, config, flutterSurface, …)`.
- `eglMakeCurrent(display, windowSurface, windowSurface, context)`.

### 3.3 Camera OES texture on this context (Kotlin)
Now that a context is current, move `createOesTexture()` +
`SurfaceTexture(oesTex)` creation here (from `CameraStream`), and point Camera2
at `Surface(cameraSurfaceTexture)`. `setOnFrameAvailableListener(..., glHandler)`
so callbacks land on the render thread.

### 3.4 Per-frame loop (Kotlin → native → swap)
On `onFrameAvailable` (render thread):
1. `cameraSurfaceTexture.updateTexImage()` and
   `getTransformMatrix(texMatrix)`.
2. Call native to render: OES texture → test shader → **default framebuffer**.
   Pass the OES texture name and the 4x4 `texMatrix`.
3. `eglSwapBuffers(display, windowSurface)` → the frame lands in the Flutter
   `SurfaceTexture`; Flutter's raster thread composites `Texture(entry.id())`.

### 3.5 Native changes (small)
- `nativeCreateSession` must run on the render thread (so
  `eglGetCurrentContext()` is the render context the `GlesRenderContext` wraps),
  **or** pass the `EGLContext`/`EGLDisplay` explicitly to `car_p0_create`
  (already in `CARPhase0Config`).
- `Phase0Session::processFrame` should bind the **default framebuffer**
  (`ctx_->bindFramebuffer(nullptr)`) instead of `outputFbo_`, and draw there.
  Drop `outputFbo_` for display (keep it only if an offscreen copy is needed
  later for effects).
- Add an **OES passthrough shader variant**: `samplerExternalOES` +
  `#extension GL_OES_EGL_image_external : require`, and apply the `texMatrix` to
  the UVs. `ShaderProgram::bindTexture` must bind `GL_TEXTURE_EXTERNAL_OES` for
  the camera input (the compute path already does this; mirror it).
- Keep `getOutputDimensions()` returning the surface size so the widget sizes
  correctly. `outputTextureId` stays `surfaceEntry.id()` — it now actually
  receives pixels.

### 3.6 Touch points summary
| File | Change |
|---|---|
| `CameraStream.kt` | Move OES/camera setup onto the GL render thread; expose the texture + transform |
| `CommunityARPlugin.kt` | Own the GL thread + EGL context + window surface; drive the frame loop; `createSession` on the GL thread |
| `jni_bridge.cpp` | `nativeSubmitFrame` passes the transform matrix; render on the GL thread |
| `phase0_session.cpp` | Render to default framebuffer + OES sampler + transform; present via swap (or Kotlin swaps) |
| `gles_render_context.cpp` | OES passthrough shader; `bindTexture` OES target |
| C ABI (`community_ar_phase0_api.h`) | Optionally a `texMatrix` param on submit-frame |

---

## 4. Sizing, rotation, mirroring
- Display size = the `SurfaceTexture` default buffer size (currently
  `1280x720`). Match the window surface + `getOutputDimensions` to it.
- Portrait devices report `SENSOR_ORIENTATION = 90/270`; either swap
  width/height for the widget (as `processFrame` already does) **and/or** bake
  rotation into the vertex/UV transform.
- Front camera needs a horizontal mirror. Fold this into the same UV transform.
- Prefer applying camera rotation/mirror via the `SurfaceTexture` transform
  matrix + a small extra transform, not by swapping buffers.

---

## 5. On-device verification checklist
Because none of this is verifiable off-device, check in this order and watch
`adb logcat`:

1. **Context creates:** log `eglGetError()` after context/surface creation; look
   for `EGL_SUCCESS`. A black screen with EGL errors here = config/surface issue.
2. **Camera frames arrive:** log in `onFrameAvailable`; confirm it fires ~30/s.
3. **updateTexImage succeeds:** no exceptions; transform matrix non-identity.
4. **Draw + swap:** log `glGetError()` after the draw and `eglSwapBuffers`
   return value. `false` swap = surface/thread mismatch.
5. **Widget shows something:** first target is `CAR_TEST_MODE_PASSTHROUGH`
   showing live camera. Then grayscale/invert/vignette to prove the shader path.
6. **Orientation/mirror:** verify upright + correct handedness on front/back.

Common failure modes: GL calls on the wrong thread (no current context);
sampling an OES texture with `sampler2D` (garbage/black); missing transform
(cropped/upside-down); window surface created before `setDefaultBufferSize`.

---

## 6. After Phase 0 display works
Only once the camera displays does the rest of the integration become testable,
in this order (see `CONSOLIDATION_AND_BRINGUP.md` §5–6):
1. Phase 2/3 method-channel handlers + JNI (`car_p2_graph_set`, `car_p3_*`, the
   Phase 1 debug/stat/filter calls) so effects install.
2. Wire the render loop to `renderFramePhase2()` so the effect graph + perception
   actually run per frame (instead of the Phase 0 test-shader path).
3. Vendor TFLite (`tools/fetch_tflite.sh`) + bundle/extract models + plumb the
   model directory so perception can load models.

Each is its own device-iteration loop.
