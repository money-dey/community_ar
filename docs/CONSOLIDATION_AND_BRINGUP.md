# Consolidation & Build Bring-Up — Log and Status

A detailed record of the work that took Community AR from *"documented as
Phases 0–3 complete, but does not build"* to *"the C++ core compiles, the
Flutter plugin has a real project structure, and a device build reaches and
compiles the native library."*

It also states, honestly, **what is not done yet** — most importantly that the
AR features are not yet wired through the Android platform layer, so on-device
testing today only exercises the Phase 0 camera pipeline.

> Scope of this document: PRs #1–#13 (all merged into `master`). Written
> 2026-07-02.

---

## 1. Where the repo started

The repository contained a large, well-commented, hand/AI-authored source tree —
Dart public API, ~11k lines of C++ core, Kotlin/Swift adapters, an example app —
and documentation describing Phases 0–3 as shipped. **But it did not build at
all**, and there was no way to run it:

- **The "delta document" pattern was never consolidated.** Throughout the
  codebase, Phase-N changes were written as separate `*_phase3_updates.{h,cpp}`
  files that *described* additions to canonical headers/classes but were never
  merged in. Several were pure comments (`// Documentation-only`). The compiled
  sources depended on interfaces that existed only in those comments.
- **Some code was written against APIs that never existed** — e.g. the segmenter
  backends used a `NeuralBackend` API (`runInference`/`ModelConfig`/
  `unloadModel`) unrelated to the real `loadModel()→NeuralModel` interface the
  TFLite/CoreML backends implement.
- **Whole types/classes were undefined** — `PerceptionInputs`, the
  `PerceptionPipeline::Impl`, and the `Phase0Session::Phase2Members` were
  referenced but never defined; constructors/destructors were missing.
- **No Flutter/Gradle/Xcode build scaffolding existed.** The plugin had source
  but no `build.gradle`/podspec; the example app had no `android/` or `ios/`
  runner projects.
- **No tests, no CI, no build had ever run.** (Consistent with a never-compiled
  tree.)

The git history at the start was two commits: an initial import and a
"Handing off to Claude Code" commit.

---

## 2. Verification methodology (and its honest limits)

Because there is no device, no vendored TensorFlow Lite, and no macOS/Xcode in
this environment, verification was layered and its boundaries were stated on
every change:

| Tool | What it proves | Used for |
|---|---|---|
| **Android NDK clang** `-fsyntax-only` (`aarch64-linux-android24`) | A translation unit compiles against the real toolchain + headers (incl. GLES/EGL/JNI from the NDK sysroot) | Every native C++ change; a 25-TU "scoreboard" of platform-agnostic sources |
| **`flutter analyze` / `dart analyze`** | Dart compiles against the real Flutter SDK + public API | The example app and library Dart |
| **`flutter build apk --debug`** | The whole Gradle → CMake → NDK chain configures and compiles the native library on real toolchain | One-time validation that the project structure is correct (PR #9) |

**What was NOT verified anywhere:** runtime behavior. No model has run, no
shader output was inspected, no mask correctness was checked, no performance was
measured, and iOS/Metal was never built. "Compiles" is a floor, not a ship.
Every PR body states this explicitly.

A key mid-campaign discovery: the **NDK clang's sysroot ships the GLES/EGL
headers**, so the GLES render-backend files *are* compile-verifiable here — which
is how the render-layer bugs and the render-context reconstruction (PRs #10–#11)
were caught and fixed with confidence.

---

## 3. What was done — by theme

### A. The native compile campaign (C++ core → 25/25 + GLES layer)

The bulk of the work: consolidating the unmerged deltas and fixing real bugs so
the entire platform-agnostic C++ tree and the Android GLES/JNI layer compile.

- **#1 — Phase 3 interface consolidation.** Merged the Phase 3 deltas that were
  only comments: defined `PerceptionInputs` for real (removing a broken include
  of a never-created `effects/effect.h`); added the `Effect` interface's
  `passOrder()`, `maskRequirements()`, and mask-pool `prepare`/`render` overloads
  (making the Phase 2 methods non-pure so pool-only effects stay concrete); added
  `PerceptionFrame::segmentationMasks`; folded `RenderContextEx` into base
  `RenderContext`; fixed `blitTextureToFramebuffer`→`blit`. **Result: the effect
  graph + 9-pass beauty pipeline compile.**
- **#2 — `FaceTracker` header (Phase 1).** A nested-type default-member-init used
  in an in-class default argument (ill-formed) + missing `<memory>`/`<functional>`
  includes. Unblocked 3 TUs.
- **#3 — `pnp_solver.cpp` (Phase 1).** Missing `#include <vector>`.
- **#4 — Segmenter backends → canonical `NeuralBackend` API.** Rewrote
  `multiclass_segmenter_backend` and `hair_segmenter_backend` and fixed
  `perception_models.cpp` (missing `namespace` opener; 6-arg → `CameraInputRect`
  `setInputTexture`) to use the real `loadModel()→NeuralModel` interface.
- **#5 — `PerceptionPipeline` reconstruction.** The Phase 3 integration was never
  built: `Impl` was never defined; ctor/dtor/`unloadIdleModels` were undefined;
  two source files disagreed on the state model. Added a shared
  `perception_pipeline_impl.h`, wired `run()`→`runSegmenterForFrame()`, added a
  `RenderContext*` to the constructor for ctx-provisioning.
- **#6 — `SkinToneEstimator` (Phase 1).** A file-scope function accessed a private
  nested `Impl`; made it a member.
- **#7 — Phase 2 session reconstruction (→ 25/25).** Added the entire
  `Phase0Session` Phase 2 surface (the `Phase2Members`/`p2_` wiring, `effectGraph()`
  /`perceptionPipeline()`/`neuralBackend()`/render-loop accessors), two additive
  `CARStatus` codes, and fixed a move-only-lambda-into-`std::function` bug in the
  Phase 2 FFI. **All 25 platform-agnostic TUs now compile.**
- **#10 — `gles_compute.cpp` (Android).** Surfaced by the first real APK build:
  missing `<GLES2/gl2ext.h>` (`GL_TEXTURE_EXTERNAL_OES`) and a `const` method
  assigning a non-`mutable` member.
- **#11 — GLES render-context consolidation.** Merged
  `gles_render_context_phase3_updates.cpp` (which defined members of a class it
  couldn't see, under wrong names) into `gles_render_context.cpp`; added the
  `GlesFramebuffer` FBO-adopt constructor the MRT path needs; removed the dead
  delta file. **After this the whole native codebase compiles except
  `tflite_backend.cpp`, which needs vendored TFLite.**

### B. Example app (#8)

Rewrote `example/lib/main.dart` into a comprehensive showcase / device-test
harness that exercises **every exported Dart capability**: camera switch; all 9
beauty presets + explicit quality tier + live sliders for all 12
`BeautyFilterConfig` knobs; lips color + opacity/edge-softness/luminance; debug
overlays (landmarks/mesh/iris/hair/pose/skin-tone) with `forcePerception`
wiring; One-Euro tuning; and a live perception-stats HUD. `dart analyze` clean.

### C. Flutter/Gradle/Xcode structure (#9)

The repo had source but no build scaffolding. Regenerated the standard plugin +
example structure with `flutter create` (preserving all existing source), then
reconciled it: kept the `dev.communityar` package (matching the JNI symbols),
**wired `externalNativeBuild` to the repo-root `CMakeLists.txt`**, cleaned the
generated stubs, fixed the podspec/pubspec iOS layout, added a `.gitignore`.
**Verified by `flutter build apk` configuring Gradle and compiling the C++ core
through the NDK** — the first time the project ever built.

### D. TensorFlow Lite vendoring scaffold (#12)

`tflite_backend.cpp` links the TFLite C API + GPU delegate **and** the delegate's
GL-interop API (`delegates/gpu/gl/*`, `TfLiteGpuDelegateBindGl*`), which is *not*
in the standard AAR. Added: a guarded `CMakeLists.txt` block consuming
`third_party/tensorflow-lite/{include,lib/<ABI>}` (clear `FATAL_ERROR` → run the
script when absent); `tools/fetch_tflite.sh` (vendors headers via a pinned sparse
checkout, prints the per-ABI Bazel build commands for the `.so`'s); a
`third_party/tensorflow-lite/README.md`; `.gitignore` for the fetched artifacts.

### E. Runtime-correctness fix (#13)

`SegmenterImpl` (hair/selfie) read its output mask to a CPU buffer and had the
re-upload **commented out** — returning an uninitialized texture. Fixed the
zero-copy way: bind the output tensor directly to the GPU mask texture
(`bindOutputTexture`), matching the multiclass backend and invariant 1.

---

## 4. Current state

**Compiles (NDK clang / real APK build):**
- All 25 platform-agnostic C++ TUs (effects, beauty, perception, math, session,
  FFI).
- The Android GLES/JNI layer (`gles_compute`, `gles_render_context`, `jni_bridge`).

**Builds far enough to matter:** `flutter build apk` configures Gradle and
compiles the native core through CMake/NDK. It stops at the link step only
because **TFLite is not yet vendored** (`tflite_backend.cpp`), which is now a
dependency/setup task, not a code bug.

**Structurally complete:** the plugin + example have a real, standard Flutter
project layout; the example exercises the whole public Dart API; `dart analyze`
is clean.

**Not verified anywhere:** any runtime behavior (masks, shaders, perception,
performance), and anything iOS/Metal.

---

## 5. The integration gap — AR features are not wired to the platform

This is the most important thing to know before device testing. The C++ Phase
2/3 features **compile** but are **not connected to the Android app**:

- **Method channel:** the Dart app invokes **17** methods; the Kotlin plugin
  handles **8** (`createSession`, `startCamera`, `switchCamera`, `setTestMode`,
  `outputTextureId`, `outputDimensions`, `getStats`, `dispose`). The 9 unhandled
  ones are the entire AR surface — `setEffectGraph`, `clearEffectGraph`,
  `getPerceptionStats`, `forcePerception`, `setDebugOverlay`, `setOneEuroParams`,
  `getMaskPoolNames`, `getBeautyEffectiveQuality`, `getEffectCount` — and no-op.
- **JNI bridge:** `jni_bridge.cpp` exposes only the Phase 0 C ABI. The Phase 2/3
  C ABI (`car_p2_graph_set`, `car_p3_*`) is never called from Android.
- **Render loop:** camera frames run the **Phase 0 test-shader path**; the
  reconstructed `renderFramePhase2()` (perception + effect graph) is never
  invoked.
- **Models:** no asset bundling/extraction, and `neuralBackend()` is built with
  an **empty `modelDirectory`**, so no model can load.

**Consequence:** after TFLite is built, a device run exercises the **Phase 0
pipeline** (live camera, the 4 test shaders, output texture, frame timing) — a
real foundation smoke test — but **none of the AR effects/perception**.

---

## 6. Remaining work (prioritized)

1. **Vendor TensorFlow Lite** (device bring-up). Run `tools/fetch_tflite.sh`;
   build the `.so`'s from source per ABI. **Confirm `TfLiteGpuDelegateBind
   GlTextureToTensor` exists** in the pinned version — the texture-binding
   variant is experimental and may need a version bump or a switch to the
   buffer-binding path. *Gates any build.*
2. **Platform integration layer** (turns "compiles" into "testable"):
   - Phase 2/3 JNI bridge functions + matching Kotlin method-channel handlers.
   - Wire the render loop to call `renderFramePhase2()` when an effect graph is
     installed.
   - Model **bundling + first-launch extraction** and **model-directory
     plumbing** into `BackendConfig` (C ABI / JNI / Kotlin / Swift).
3. **iOS bring-up:** the podspec's native-C++ wiring; Metal `.mm` verification
   (needs macOS/Xcode).
4. **Runtime validation** (only possible after 1–2): the multiclass
   output-texture format assumption; cross-skin-tone/hair mask quality (the M7
   gate in `docs/car-phase-3-dag.md`); performance budgets; the beauty pipeline
   visual results.
5. **Test/CI infrastructure** described in `TESTING.md` (still absent).

---

## 7. PR index

All merged into `master`.

| PR | Title | Theme |
|---|---|---|
| #1 | consolidate Phase 3 interface deltas so the effect/beauty layer compiles | compile |
| #2 | make FaceTracker header compile (Phase 1) | compile |
| #3 | add missing `<vector>` to pnp_solver.cpp | compile |
| #4 | align segmenter backends to the canonical NeuralBackend API | compile |
| #5 | reconstruct the PerceptionPipeline Phase 3 integration | compile |
| #6 | make SkinToneEstimator readback polling a member | compile |
| #7 | reconstruct the Phase 2 session integration (25/25 compiling) | compile |
| #8 | showcase the full public Dart API in one example harness | example |
| #9 | add missing Flutter plugin + example platform scaffolding | structure |
| #10 | fix two build errors in gles_compute.cpp | compile (GLES) |
| #11 | consolidate the GLES render-context Phase 3 delta | compile (GLES) |
| #12 | scaffold TensorFlow Lite vendoring for the native build | build/deps |
| #13 | bind segmenter output to GPU texture instead of stubbed readback | runtime-fix |

---

## 8. A note on the "delta document" pattern

The single recurring root cause was the project's convention (see `CLAUDE.md`)
of writing Phase-N changes as separate `*_updates` delta files "to be merged in
a later consolidation pass" — a pass that never happened. Wherever you see a
`*_phase*_updates.*` file remaining, treat it as **already consolidated**; the
ones that were load-bearing have been merged and deleted (perception pipeline,
GLES render context), and the remaining `*_phase2_updates.cpp` /
`*_phase3_updates.cpp` files that still compile are real code, not deltas.
