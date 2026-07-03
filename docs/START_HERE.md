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
2. **[`RENDER_PIPELINE_OWNERSHIP.md`](RENDER_PIPELINE_OWNERSHIP.md)** — the
   load-bearing decision behind the whole render layer: *who owns the GPU
   context + presentation surface* (platform vs. C++), why **Option A (platform
   owns it)** was chosen on both platforms, the challenges/tradeoffs hit
   implementing it, and how to reverse it if needed. Read before touching the
   pipeline on either platform.
3. **[`ANDROID_RENDER_PIPELINE.md`](ANDROID_RENDER_PIPELINE.md)** — Android
   GL/EGL: what was broken, the Option-A architecture (now implemented, PR #22),
   file touch points, and the ordered on-device verification checklist.
4. **[`IOS_RENDER_PIPELINE.md`](IOS_RENDER_PIPELINE.md)** — iOS Metal: the
   analogous (still-unimplemented) black-preview gap, the pull-based Option-A
   target architecture, plan + touch points, and an on-device checklist. Needs a
   Mac; not verifiable in this dev env.
5. **[`../CLAUDE.md`](../CLAUDE.md)** — architectural invariants and the
   "delta document" convention that caused the original non-compiling state.
6. **[`CARRIED_FORWARD.md`](CARRIED_FORWARD.md)** — pre-existing deferred items.

---

## What to pick up next (priority order)

0. ✅ **Android GL/EGL pipeline → visible camera — DONE & verified on-device**
   (PRs #22–#25): live, upright, un-mirrored, zoomable, resumes after background.

1. **AR feature integration (Phases 0–3) → see
   [`AR_INTEGRATION_SPEC.md`](AR_INTEGRATION_SPEC.md).** The full plan: inventory
   of every perception/effect feature, the render-loop integration architecture
   (OES→2D ingress, `renderFramePhase2` + present-blit, model-dir plumbing), and
   ordered work packages **WP-A…WP-E** (TFLite+models → render loop → effect graph
   → beauty → perception diagnostics), each implementation-ready with acceptance
   criteria. This is the spec to hand an implementer.

   The old summary of the same work, for reference:
   - **TFLite + models** (WP-A) — vendor TFLite (`tools/fetch_tflite.sh`), bundle/
     extract models (`tools/fetch_models.sh`), plumb `modelDirectory`.
   - **Phase 2/3 platform wiring** (WP-C/D) — Kotlin handlers + JNI for
     `car_p2_graph_set` / `car_p3_*` / Phase 1 calls; render loop →
     `renderFramePhase2()`.

Each WP is its own device-iteration loop; do them in order (nothing later is
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
