# START HERE

Single entry point for anyone — a new contributor, you resuming after a break,
or a fresh Claude Code session — picking up Community AR. Read this first, then
follow the links in order.

---

## 30-second state (2026-07-03)

Community AR is a Flutter face-AR plugin: a thin Dart API over a large
C++/GLES/Metal core. It arrived **documented as Phases 0–3 complete but not
building**; the C++ core has since been consolidated so it compiles, and the app
now builds and runs.

- ✅ **Compiles:** all 25 platform-agnostic C++ TUs + the Android GLES/JNI layer.
- ✅ **Builds & runs:** `flutter build apk --debug` produces an installable APK;
  the app launches and requests camera permission.
- 🟡 **Android GL/EGL pipeline is now built (Option A), but not runtime-verified.**
  Kotlin owns a GL render thread + EGL context + a window surface from the Flutter
  `SurfaceTexture`; the camera OES texture lives on that context; each frame
  renders into fbo 0 with a `samplerExternalOES` shader + UV transform and
  presents via `eglSwapBuffers` (`GlRenderPipeline.kt` + native display path).
  Compiles & links; **the on-device verification checklist
  ([`ANDROID_RENDER_PIPELINE.md`](ANDROID_RENDER_PIPELINE.md) §5) has NOT been run**
  — no device/GPU/camera in the dev env. Orientation/mirror + display sizing still
  need on-device tuning. **This is the next thing to verify on hardware.**
- ⛔ **AR features (effects/perception) aren't wired to the platform.** The Kotlin
  method channel handles only Phase 0 methods; no Phase 2/3 JNI; the render loop
  never calls `renderFramePhase2()`; TFLite/models aren't set up.

> Honesty rule for this project: separate **"compiles"** from **"works."** Almost
> nothing here is runtime-verified (no device, no TFLite, no iOS in the dev env).
> Don't claim GPU/camera/perception behavior works — it's untested.

---

## Read these next, in order

1. **[`CONSOLIDATION_AND_BRINGUP.md`](CONSOLIDATION_AND_BRINGUP.md)** — the master
   status/log: what each PR did, the verification methodology + limits, current
   state, and the platform-integration gap. **The main status doc.**
2. **[`ANDROID_RENDER_PIPELINE.md`](ANDROID_RENDER_PIPELINE.md)** — the
   black-preview blocker: what's broken, target architecture (recommended:
   Kotlin owns EGL), step-by-step plan + file touch points, and an ordered
   on-device verification checklist.
3. **[`../CLAUDE.md`](../CLAUDE.md)** — architectural invariants and the
   "delta document" convention that caused the original non-compiling state.
4. **[`CARRIED_FORWARD.md`](CARRIED_FORWARD.md)** — pre-existing deferred items.

---

## What to pick up next (priority order)

1. **Android GL/EGL pipeline → visible camera — VERIFY ON DEVICE.** The pipeline
   is now *implemented* (Option A, `GlRenderPipeline.kt` + native display path);
   what remains is walking the on-device checklist in `ANDROID_RENDER_PIPELINE.md`
   §5 (context creates → frames arrive → `updateTexImage` → draw+swap → widget
   shows live camera → orientation/mirror) and tuning the UV transform + display
   sizing for real hardware. This still gates *all* visible output.
2. **Phase 2/3 platform wiring** — Kotlin method-channel handlers + JNI for
   `car_p2_graph_set` / `car_p3_*` / the Phase 1 debug/stat/filter calls, and
   wire the render loop to `renderFramePhase2()`, so effects/perception run.
3. **TFLite + models** — vendor TFLite (`tools/fetch_tflite.sh` +
   `third_party/tensorflow-lite/README.md`), bundle/extract the MediaPipe models
   (`tools/fetch_models.sh`), and plumb the model directory into `BackendConfig`.

Each is its own device-iteration loop; do them in order (nothing later is
testable until #1 works).

---

## Dev environment — what can and can't be verified here

- **No Android device, no camera/GPU runtime, no macOS/Xcode.** GL/EGL/Metal/JNI
  and perception are **not runtime-verifiable**; iOS can't be built.
- **No vendored TensorFlow Lite.** TFLite is *optional* in the build (a stub
  backend links when absent, so the app builds without it). Enabling it needs a
  from-source Bazel build.
- **New Gradle/Maven downloads fail** (`PKIX path building failed` SSL/cert issue
  + intermittent TLS timeouts). **Do not add new pub/Gradle dependencies** —
  cached artifacts build fine. (This is why `permission_handler` was replaced
  with a native `ActivityAware` permission request.)

### Tooling that works
- **NDK clang** (`-fsyntax-only`, `--target=aarch64-linux-android24`,
  `-Inative/core -Inative/core/ffi`) — its sysroot ships GLES/EGL/JNI headers, so
  it compiles the platform-agnostic C++ *and* the Android GLES/JNI TUs. Path:
  `…/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe`.
- **Flutter/Dart** 3.44.2 (`C:\SDKs\flutter\bin\flutter.bat` / `dart.bat`);
  `flutter build apk --debug` is the real compile+link+native check.
- **`gh` CLI** (`C:\Program Files\GitHub CLI\gh.exe`); remote
  `money-dey/community_ar`.

---

## Working conventions (that have worked well here)
- One focused fix per branch/PR; conventional-commit title; DCO `-s` sign-off;
  `Co-Authored-By: Claude Opus 4.8`.
- Verify with NDK clang / `flutter build apk` / `dart analyze`; state the
  "not runtime-verified" boundary in every PR body.
- The user reviews and merges each PR; sync `master` before starting the next.

---

## Resuming in a new Claude Code session
Open a session **in this repo**; the project memory files load automatically and
point back here. A good kickoff line:

> "Read docs/START_HERE.md and the docs it links, then let's continue with
> [the Android GL pipeline / next task]."
