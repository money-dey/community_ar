# Render Pipeline Ownership — Who Owns EGL/Metal, and Why

**Status:** Decided & implemented for Android (Option A). iOS follows the same
principle (see [`IOS_RENDER_PIPELINE.md`](IOS_RENDER_PIPELINE.md)).
**Decision date:** 2026-07-03. **Related:** PR #22,
[`ANDROID_RENDER_PIPELINE.md`](ANDROID_RENDER_PIPELINE.md), CLAUDE.md §1–§2.

This is an Architecture Decision Record. It exists because "who owns the GPU
context and the presentation surface" is a load-bearing choice that shapes the
JNI/FFI boundary, the threading model, cross-platform symmetry, and how
debuggable on-device bring-up is. Getting it wrong is expensive to undo once
effects and perception are layered on top, so the reasoning is written down.

---

## 1. The question

A camera frame has to travel: **camera → GPU processing → a Flutter texture on
screen.** Someone must own the GPU *context* (EGL on Android, the Metal
device/command queue on iOS) and the *presentation surface* (the EGL window
surface / the Flutter-read pixel buffer). Two candidates:

- **Option A — the platform layer owns it** (Kotlin on Android, Swift on iOS).
- **Option B — the C++ core owns it** (EGL/Metal created and driven from native,
  platform hands over a bare surface + camera frames).

We chose **Option A** on both platforms.

---

## 2. The reframe: less differs than the framing suggests

The most important thing to understand is that **the render *engine* lives in
C++ in both options and does not move.** `RenderContext` (and its
`GlesRenderContext` / `MetalRenderContext` implementations), every shader, the
effect engines, mask rasterization, and perception are C++ regardless. This is
fixed by CLAUDE.md §1 ("pixels never enter Dart") and the mental model that C++
is the substantial engine.

`GlesRenderContext` is *designed to receive* an `EGLContext`/`EGLDisplay`, and
`MetalRenderContext` is *designed to receive* an `MTLDevice`. They wrap a
context; they don't create one.

So the A-vs-B choice only moves **~150 lines of platform glue**:

| Concern | Moves with the decision? |
|---|---|
| EGL context / Metal device **lifecycle** | ✅ yes |
| The **presentation surface** (EGL window surface / Flutter pixel buffer) | ✅ yes |
| The **camera texture** plumbing (`SurfaceTexture` / `CVMetalTextureCache`) | ✅ yes |
| The **frame-loop driver** (who calls "render this frame") | ✅ yes |
| `RenderContext`, shaders, effects, perception, GPU resource mgmt | ❌ no — always C++ |
| The C ABI stability boundary | ❌ no — unchanged either way |

Because the stakes are "where does the glue live," not "which language does the
rendering," the decision is also **reversible** — see §7.

---

## 3. Why Option A (platform owns the context)

### 3.1 Cross-platform symmetry — the strongest reason
This is a cross-platform plugin. On **iOS the platform necessarily owns the GPU
handles**: `MTLCreateSystemDefaultDevice()` is called in Swift, the Flutter
`FlutterTexture` protocol is Swift, and camera frames arrive as
`CVMetalTextureRef` from `AVCaptureSession` — all Apple platform APIs. There is
no version of iOS where C++ "creates the device" first; Swift makes the device
and hands it down (`cfg.gpuContext = device`, see `CommunityARPlugin.swift`).

If Android also has the platform own EGL, both platforms share one shape:

> **platform owns { context, presentation surface, camera } · C++ owns the
> render engine · the C ABI is "here is a context, here is a camera texture,
> render into this target."**

Option B would make Android asymmetric with iOS for no structural gain, forking
the mental model and the C ABI contract per platform.

### 3.2 Option B would *increase* the JNI/FFI boundary, not shrink it
The intuitive case for B is "keep the whole GPU path in one language." But
Camera2, `SurfaceTexture`, `Surface`, and `TextureRegistry` are **Java APIs**.
If C++ owned EGL you would *still* need Kotlin to:
- hand the Flutter `Surface` down to native, and
- bounce every Camera2 frame callback across JNI onto the native render thread.

That's **more** boundary crossings and cross-language thread ownership. Under A,
the camera texture and window surface stay in Kotlin and only a per-frame
"render" call crosses into C++ carrying a texture name + a 4×4 matrix.

### 3.3 On-device bring-up is where the hard bugs are, and the tooling is native-platform-side
The class of bug that dominates this work — no current context, wrong-thread GL
calls, surface/thread mismatch, `eglSwapBuffers` returning false — is diagnosed
with `adb logcat`, EGL error codes, and the Android GPU inspector, all
Kotlin/Java-facing. Debugging an EGL surface mismatch *through JNI in C++* is
materially harder. Since bring-up is the current blocker and is inherently a
device write→test→fix loop, keeping EGL where the tooling is strongest is worth
a lot.

### 3.4 Single-thread context ownership is cleaner
EGL contexts are thread-affine: a context is current on exactly one thread, and
GL calls only work on that thread. Under A, one Kotlin `HandlerThread`
("CommunityAR-GL") owns the context, the window surface, the camera OES texture,
and the per-frame draw — one owner, one thread, no marshalling. Under B, C++
owns the render thread and Kotlin must marshal camera frames onto it, adding a
hand-off that's easy to get subtly wrong.

---

## 4. Option B's genuine merits (and when it would win)

Steel-manning the rejected option:

- **"GPU resource management happens in C++"** is a stated principle; context
  lifecycle is arguably part of it.
- **One language for the whole GPU path** is conceptually tidy — no lambdas
  bouncing into Kotlin to create/destroy the native session.
- **Native control of frame-loop timing.** If you wanted the render loop driven
  by a native display-link / `Choreographer`-equivalent rather than the camera
  frame callback, owning the context in C++ is cleaner.

**Option B would be the right call if** the platform layer were meant to be a
truly dumb shell with a *single unified C++ render path* across platforms, and
you were willing to pay the extra JNI + harder-to-debug cost — e.g. if you
dropped Flutter's texture layer, or needed native-side frame pacing. None of
that is true here today.

---

## 5. Challenges & tradeoffs actually hit implementing Option A (Android)

These are the real, concrete tensions — the things that made this "a from-scratch
GL integration, not a one-line handoff." They're the reason this doc exists.

1. **`GlesRenderContext`'s constructor issues GL calls.** It creates a VAO/VBO
   and compiles shaders immediately. So the **native session must be created on
   the GL thread, after `eglMakeCurrent`** — not on the platform thread. The
   pipeline posts `nativeCreateSession` onto its GL thread for exactly this
   reason. *(Tradeoff: session creation is asynchronous relative to the Dart
   `createSession()` call — see #2.)*

2. **`start()` briefly blocks the platform thread on a `CountDownLatch`.** The
   Dart widget calls `createSession()` then immediately `startCamera()`, which
   needs the camera `SurfaceTexture` that's created on the GL thread. Rather than
   thread an async-ready callback through the method channel, `start()` posts GL
   setup and waits (≤5s) so callers can synchronously grab
   `cameraInputSurface()`. *Tradeoff:* a one-time few-ms block on the platform
   thread, accepted because it's setup-only and vastly simpler than a
   ready-state state machine.

3. **Native session teardown also issues GL calls** (`glDeleteBuffers`,
   `glDeleteVertexArrays`, shader deletes). So `nativeDestroySession` **must also
   run on the GL thread** with the context current. `teardown()` in the plugin
   does *not* call destroy directly; it calls `pipeline.release()`, which posts
   destroy → EGL teardown → thread quit onto the GL thread. Getting this wrong
   would leak or crash on dispose.

4. **Cross-context texture validity.** The original sketch created the camera OES
   texture on the camera thread with no context, then passed the raw GL name to a
   *different* native context — invalid. Option A dissolves the bug by
   construction: the camera OES texture is created on the **same** context native
   renders on, so its GL name is valid native-side with no sharing/`EGLImage`
   dance.

5. **C ABI stability vs. behavior change.** The display path needs to render into
   fbo 0 (the window surface) with a UV transform, but the shipped
   `car_p0_submit_frame` renders into an offscreen FBO. CLAUDE.md §2 forbids
   changing a shipped symbol. **Resolution:** add a *new* symbol
   `car_p0_submit_frame_display` (with a `texMatrix`) and leave the old one
   untouched. *Tradeoff:* two submit paths coexist until the offscreen one is
   needed again for effects; that's the cost of an immutable ABI.

6. **Who calls `eglSwapBuffers`.** Because Kotlin owns the window surface, Kotlin
   swaps — native just renders into fbo 0 and returns. This keeps native free of
   any window-surface knowledge (it never sees the `EGLSurface`), reinforcing the
   clean split, at the cost of the "present" step living in a different language
   from the "render" step.

7. **Orientation & sizing are device-dependent and unverifiable here.**
   `computeUvTransform` composes the `SurfaceTexture` transform with a
   rotation+mirror about the UV center, and the display buffer is fixed at
   720×1280 portrait. The *mechanism* is in place, but exact handedness and
   portrait/landscape sizing need the on-device checklist. This is honest
   technical debt, flagged in code and in `ANDROID_RENDER_PIPELINE.md` §4–5.

8. **Thread-safety of the non-render calls.** `setTestMode` writes a
   `std::atomic`, and `getStats`/`getOutputDimensions` read behind a mutex, so
   those can be called from the platform thread without hopping to the GL thread.
   Only calls that touch GL state go through the GL thread. Knowing which calls
   are which is a small ongoing burden the split imposes.

---

## 6. What "not runtime-verified" means for this decision

The dev environment has no Android device, GPU, or camera, and cannot build iOS.
So Option A is **compile/link-verified** (NDK clang `-fsyntax-only` + `flutter
build apk --debug`) but not proven to put pixels on screen. The architectural
reasoning above is sound independent of that, but the specific failure modes that
*would* argue for revisiting (e.g. a driver quirk with the cross-thread window
surface, or `eglSwapBuffers` into a Flutter `SurfaceTexture` misbehaving on a
given vendor GPU) can only surface on hardware. If one does, see §7.

---

## 7. Consequences & reversibility

- **Positive:** minimal + symmetric C ABI; one thread owns GL; camera/surface
  plumbing stays in the language whose SDK owns those types; debuggable bring-up.
- **Negative:** GPU-path logic is split across two languages; a few calls must be
  routed to the GL thread; native session lifecycle is asynchronous.
- **Reversibility (low-regret):** because the C ABI already treats the context as
  *externally owned*, switching Android to Option B later means: move EGL creation
  from `GlRenderPipeline.kt` into a native factory, add a `Surface`-handoff JNI
  call, and move the render thread into C++. The engine, shaders, effects, and
  the C ABI shape do **not** change. So this decision does not lock the project in.

---

## 8. Cross-platform mapping

The same principle, different platform primitives. iOS has no thread-affine
context and is *pull-based* (Flutter asks for a pixel buffer), so it needs no
`eglSwapBuffers` equivalent — but it must render into an IOSurface-backed texture
that Flutter reads. Full detail in [`IOS_RENDER_PIPELINE.md`](IOS_RENDER_PIPELINE.md).

| Concept | Android (GLES/EGL) | iOS (Metal) |
|---|---|---|
| GPU context owner | Kotlin (`EGL14` context on a GL thread) | Swift (`MTLDevice`) |
| Camera texture | OES `SurfaceTexture` (Kotlin) | `CVMetalTextureCache` → `MTLTexture` (Swift) |
| Presentation surface | EGL window surface from Flutter `SurfaceTexture` | `CVPixelBuffer` (IOSurface) via `FlutterTexture` |
| Present step | `eglSwapBuffers` (push) | `copyPixelBuffer` + `textureFrameAvailable` (pull) |
| Thread model | one context-affine GL thread | command buffers, largely thread-agnostic |
| Render engine | C++ `GlesRenderContext` | C++ `MetalRenderContext` |
| Ownership pattern | **Option A** | **Option A** (only natural fit) |
