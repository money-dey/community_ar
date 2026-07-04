# AR Feature Integration Spec — Phases 0–3

Implementation-ready specification for wiring the Community AR **perception +
effect** features (which already compile in C++ and are already exposed in Dart)
through the **Android platform layer** so they actually run on-device. Written to
be handed to an implementer (e.g. Claude Fable 5) as a set of ordered work
packages, each with concrete files, interfaces, steps, and acceptance criteria.

**Read first:** [`CLAUDE.md`](../CLAUDE.md) (architectural invariants),
[`RENDER_PIPELINE_OWNERSHIP.md`](RENDER_PIPELINE_OWNERSHIP.md) (why Kotlin owns
EGL), [`ANDROID_RENDER_PIPELINE.md`](ANDROID_RENDER_PIPELINE.md) (the live Phase
0 pipeline this builds on), and [`CONSOLIDATION_AND_BRINGUP.md`](CONSOLIDATION_AND_BRINGUP.md)
§5–6 (the integration gap).

> **Honesty rule (project-wide):** separate *"compiles"* from *"works."* The dev
> environment has no camera/GPU/TFLite/iOS, so every item below is
> compile/link-verifiable only; runtime correctness (models, masks, effects,
> performance) must be verified on a real device on **dark, medium, and light
> skin tones and ≥3 hair textures** (CLAUDE.md invariant 10, `TESTING.md`).

---

## 1. Current state (what you're building on)

- ✅ **Phase 0 pipeline is LIVE on Android** (PRs #22–#25): Option A EGL (Kotlin
  owns the GL thread + context + window surface), camera OES texture → OES shader
  → default framebuffer (fbo 0) → `eglSwapBuffers`. Orientation, pinch-zoom
  (hardware + digital), and background/resume all work on-device.
- ✅ **The C++ AR engine compiles** — perception pipeline, effect engines, beauty
  pipeline, mask pool (25/25 platform-agnostic TUs).
- ✅ **The Dart→method-channel side is already done** — `CommunityARView` calls
  `setEffectGraph`; the example harness calls every diagnostic. Effects serialize
  to the exact C struct layouts.
- ⛔ **The Android platform layer does NOT wire the AR surface.** The Kotlin
  plugin handles only Phase 0 methods; there is no Phase 1/2/3 JNI; the render
  loop never calls `renderFramePhase2()`; no models are bundled and
  `neuralBackend()` is built with an empty `modelDirectory`.

**Net:** this spec is almost entirely **Kotlin + JNI + native render-loop
integration + model plumbing**. Very little Dart or C++ *engine* code changes.

---

## 2. Feature inventory (Phases 0–3)

| Phase | Feature | What it is | Dart API | Native C ABI | Model(s) | Status |
|---|---|---|---|---|---|---|
| 0 | Camera pipeline + 4 test shaders | Live preview; passthrough/grayscale/invert/vignette | `CommunityARView`, `CARTestMode` | `car_p0_*` | — | ✅ integrated |
| 1 | Face detection + FaceMesh (468 landmarks) | Per-face landmark mesh | (implicit; drives effects/overlays) | perception via `car_p2_graph_set` reqs + `car_p1_force_perception` | `face_detector.tflite`, `face_landmarker.tflite` | engine ✅, not wired |
| 1 | Face blendshapes | 52 blendshape coeffs (gating, e.g. lips on smile) | — | — | `face_blendshapes.tflite` | engine ✅, not wired |
| 1 | Iris landmarks | Both eyes (left-eye model mirrored for right) | overlay `iris` | `car_p1_force_perception(needIris)` | `iris_landmark.tflite` | engine ✅, not wired |
| 1 | Hair segmentation | Hair mask | overlay `hairMask` | `...(needHair)` | `hair_segmenter.tflite` | engine ✅, not wired |
| 1 | Selfie/body segmentation | Person/background mask | — | `...(needSelfieSeg)` | `selfie_segmenter.tflite` | engine ✅, not wired |
| 1 | Head pose (PnP) | Rotation/translation via custom PnP (no Eigen/OpenCV) | overlay `pose` | `...(needPose)` | — (solver) | engine ✅, not wired |
| 1 | Skin-tone estimation | Oklab baseline luma, async GPU readback | overlay `skinTone`, stats | `...(needSkinTone)` | — | engine ✅, not wired |
| 1 | FaceTracker | Stable `faceId` (Hungarian IoU); all per-track state keys off it | — | — | — | engine ✅, not wired |
| 1 | One-Euro landmark filter | Temporal smoothing on all landmarks | `setOneEuroParams` | `car_p1_set_one_euro_params` | — | engine ✅, not wired |
| 1 | Debug overlays | landmarks/mesh/iris/hair/pose/skinTone | `setDebugOverlay`, `DebugOverlayMode` | `car_p1_set_debug_overlay` | — | engine ✅, not wired |
| 1 | Perception stats | faces, per-model ms, skin luma | `getPerceptionStats` | `car_p1_get_perception_stats` | — | engine ✅, not wired |
| 2 | EffectGraph | Pass-ordered, atomic install of effects | `setEffectGraph`/`clearEffectGraph`/`getEffectCount` | `car_p2_graph_set`/`car_p2_graph_clear`/`car_p2_graph_effect_count` | — | engine ✅, not wired |
| 2 | LipsEffect | MaskedRecolor engine, Oklab blend on lip contour | `LipsEffect` | typeId `1`, `CARLipsEffectConfig` (28 B) | needs FaceMesh | engine ✅, not wired |
| 3 | SkinSmoothEffect | 9-pass beauty; Auto/High/Med/Low tiers; tone-aware | `SkinSmoothEffect`, `BeautyFilterConfig`, `BeautyQuality`, `BeautyPresets` (9) | typeId `8`, `CARBeautyFilterConfig` (60 B) | needs FaceMesh + face-skin mask | engine ✅, not wired |
| 3 | Beauty effective quality | Resolved tier after Auto benchmark | `getBeautyEffectiveQuality` | `car_p3_beauty_effective_quality` | — | engine ✅, not wired |
| 3 | Mask resource pool | Named masks: faceSkin, hair, bodySkin, clothes, background, lipsContour… | `getMaskPoolNames` | `car_p3_mask_pool_list` | — | engine ✅, not wired |

**Perception runs on demand** (CLAUDE.md invariant 3): each effect declares its
`perceptionInputs()`; the graph computes the union; the pipeline runs only the
needed models. `car_p1_force_perception` lets the debug overlays demand models no
effect requested.

---

## 3. Integration architecture (how AR plugs into the live pipeline)

Four reconciliations turn the Phase 0 present path into the AR path. **These are
the crux — get them right before writing the per-feature glue.**

### (a) Camera ingress: OES → 2D, once per frame
Perception models and effect shaders consume `cameraOutputTexture()` — a
`TextureHandle` they sample as a normal 2D texture. Today the OES camera texture
is sampled directly by the display shader and never copied. **Add an ingress pass
that copies the OES camera texture into a 2D RGBA texture** (the reference "blit
pass" noted in `gles_render_context.cpp`), and make that 2D texture the value
returned by `Phase0Session::cameraOutputTexture()`. This runs once per frame,
before perception.

### (b) Orientation/zoom must be baked at ingress
The models are orientation-sensitive; perception must see an **upright** frame.
The ingress pass must apply the same UV transform the display path computes
(`GlRenderPipeline.computeUvTransform`: rotation + mirror + digital zoom). So:
move the transform to ingress → `cameraOutputTexture()` is upright and
digitally-zoomed, and both perception and effects operate on it. Hardware zoom
already affects the OES frame upstream, so it needs nothing extra.

### (c) Render-path swap + present
`renderFramePhase2()` runs perception then `effectGraph().render(camera, frame,
displayFramebuffer())`, where `displayFramebuffer()` is the **offscreen**
`outputFbo_`. The live pipeline instead presents **fbo 0** (the window surface).
Reconcile by ending the AR frame with a **present blit**: `outputFbo_` color
texture → fbo 0 (a passthrough `sampler2D` quad), then Kotlin `eglSwapBuffers`.
Keep effects rendering into the offscreen FBO (they need it for ping-pong), and
keep "present" in one place.

> **Decision to make (recommend option 1):**
> 1. **New ABI `car_p2_submit_frame_display(session, oesTex, w, h, texMatrix16,
>    timestampNs)`** that does ingress → (if graph non-empty) `renderFramePhase2`
>    → present-blit, else presents the ingress texture directly. Kotlin calls
>    this instead of `car_p0_submit_frame_display` once AR is enabled. Subsumes
>    Phase 0; test shaders remain reachable when the graph is empty. *(Recommended
>    — one entry point, clean fallback.)*
> 2. Keep `car_p0_submit_frame_display`; add the AR path separately and branch in
>    Kotlin on `getEffectCount()`. *(More moving parts.)*
> New symbol either way — never modify `car_p0_submit_frame*` (CLAUDE.md §2).
> Note the **new `timestampNs`** param: perception + One-Euro need the frame
> timestamp (`SurfaceTexture.getTimestamp()`); the Phase 0 path didn't pass it.

### (d) Model directory plumbing
`neuralBackend()` builds `BackendConfig` with an empty `modelDirectory`, so no
model loads. Plumb a real path end-to-end: **bundle the `.tflite` files as app
assets → extract to `filesDir/models` on first launch → pass that path** through
`CARPhase0Config` (add a `const char* modelDirectory` — new field, versioned) →
`neuralBackend()`. TFLite itself must be vendored first (gating; see WP-A).

---

## 4. Work packages (ordered)

Dependency order: **WP-A → WP-B → WP-C → WP-D → WP-E**. WP-A/B gate everything
visible; WP-C is the first end-to-end AR effect; D and E layer on.

### WP-A — TFLite + models + model-directory plumbing *(gating)*
> **Status: IMPLEMENTED** (branch `feat/android-tflite-prebuilt`) — via the
> **prebuilt-AAR route, no Bazel**: `tools/fetch_tflite_prebuilt.sh` vendors the
> Maven Central 2.16.1 binaries (C API symbols verified with llvm-nm; the
> GL-interop Bind* symbols are absent, so `tflite_backend.cpp` stages tensor
> I/O through CPU — GPU-delegate inference, copies only at tensor boundaries;
> GL-interop kept behind `-DCAR_TFLITE_GL_INTEROP=ON` for a future from-source
> build). Models bundle as plugin assets → first-launch extraction →
> `car_p0_set_model_directory` (new symbol; `CARPhase0Config` unchanged). Also
> implemented GLES `createFramebufferForTexture` — a pre-existing nullptr gap
> that would have broken the whole effect chain. APK verified: DT_NEEDED on
> both TFLite libs + 6 model assets. On-device model loading still unverified.

**Goal:** the native neural backend can load a model on-device.
**Files:** `tools/fetch_tflite.sh`, `third_party/tensorflow-lite/*`, root
`CMakeLists.txt`, `native/core/ffi/community_ar_phase0_api.h` (add
`modelDirectory` to `CARPhase0Config`, versioned), `phase0_session_phase2_updates.cpp`
(`neuralBackend()` reads it), `android/.../CommunityARPlugin.kt` +
`jni_bridge.cpp` (pass the path), `android/build.gradle` + `example` assets,
`tools/fetch_models.sh` (already exists).
**Steps:** vendor TFLite per ABI (build `.so`s from source; **confirm
`TfLiteGpuDelegateBindGlTextureToTensor` exists** in the pinned version — the
texture-binding variant is experimental, may need the buffer-binding path);
bundle the 5 `.tflite` files (`face_detector`, `face_landmarker`,
`face_blendshapes`, `iris_landmark`, `hair_segmenter`, `selfie_segmenter`) as
assets (~<15 MB budget); extract to `filesDir/models` on first launch (idempotent,
version-stamped); plumb the directory into `BackendConfig`.
**Acceptance:** `flutter build apk` links (no TFLite-missing FATAL_ERROR); on
device, one model `loadModel()` succeeds (log the tensor shapes).
**Risks:** TFLite GL-interop flag naming varies by version (there's a TODO in
`tflite_backend.cpp`); SSL blocks new Gradle deps in *this* env (vendored `.so`s
sidestep that, but must be built on a machine with network).

### WP-B — Render-loop integration (ingress + present) *(gating for all visible AR)*
> **Status: IMPLEMENTED** (branch `feat/android-ar-render-path`) — chose §3(c)
> option 1: new `car_p2_submit_frame_display` symbol; Kotlin now calls it
> unconditionally (`nativeSubmitFrameAr`), passing `SurfaceTexture.timestamp`.
> Compile/link-verified; on-device acceptance below still pending.

**Goal:** with an empty graph, AR path presents identical live camera; with a
graph, it runs perception + effects and presents them.
**Files:** `native/core/phase0_session.{h,cpp}` (ingress pass + present blit +
`cameraOutputTexture()` returns the 2D ingress texture), `phase0_session_phase2_updates.cpp`
(`renderFramePhase2` already exists — call it), new ABI in
`community_ar_phase2_api.{h}` + impl, `jni_bridge.cpp` (`nativeSubmitFrameArDisplay`
with the `float[16]` + timestamp), `android/.../GlRenderPipeline.kt` (call the AR
submit; pass `SurfaceTexture.getTimestamp()`), `CommunityARPlugin.kt` (route).
**Steps:** implement §3(a)(b)(c). Ingress: reuse the existing OES shader
(`samplerExternalOES` + `uTexMatrix`) rendering into a 2D FBO owned by the
session, sized to the display. Present-blit: passthrough `sampler2D` quad
`outputFbo_`→fbo 0. Kotlin keeps ownership of `eglSwapBuffers`.
**Acceptance (compile):** NDK clang + `flutter build apk`. **On-device:** empty
graph = live camera unchanged; a trivial test (e.g. install LipsEffect with
opacity 0) still shows live camera; frame time logged.
**Risks:** an extra full-frame copy + blit costs ~1–2 ms (fits the 33 ms budget);
verify no double orientation (transform now at ingress, not present).

### WP-C — Phase 2 JNI + Kotlin: effect graph → LipsEffect end-to-end
> **Status: IMPLEMENTED** (branch `feat/android-effect-graph-channel`) —
> `setEffectGraph`/`clearEffectGraph`/`getEffectCount` handlers + JNI. Also made
> `car_p2_graph_effect_count` a non-constructing peek (the lazy accessor was
> only render-thread-safe). Compile/link-verified; the visual acceptance
> (lipstick) still needs WP-A's models on-device.

**Goal:** installing an `EffectGraph` from Dart makes lipstick appear.
**Method-channel methods:** `setEffectGraph{typeIds,configs}`, `clearEffectGraph`,
`getEffectCount`.
**Files:** `jni_bridge.cpp` (marshal `List<Int>` typeIds + `List<ByteArray>`
configs → `car_p2_graph_set`; `car_p2_graph_clear`; `car_p2_graph_effect_count`),
`CommunityARPlugin.kt` (handlers; graph mutations are queued onto the render
thread by the native side via `runOnRenderThread` — call from the platform
thread is fine).
**Interface mapping:**
- `setEffectGraph` → `car_p2_graph_set(session, count, uint32 typeIds[], void* configs[], size_t sizes[])`. Copy each Kotlin `ByteArray` into a native buffer; the native side copies again and frees are caller-owned.
- `clearEffectGraph` → `car_p2_graph_clear`.
- `getEffectCount` → `car_p2_graph_effect_count`.
**Acceptance (compile):** builds. **On-device:** `LipsEffect(color: red)` tints
lips on a tracked face across skin tones; `clearEffectGraph` removes it;
`getEffectCount` reflects installs. Requires WP-A (FaceMesh) + WP-B.
**Risks:** config byte-layout must match `CARLipsEffectConfig` (28 B) exactly —
the Dart `serialize()` already targets it; verify endianness on device.

### WP-D — Phase 3 JNI + Kotlin: beauty diagnostics → SkinSmoothEffect end-to-end
**Goal:** `SkinSmoothEffect` smooths skin; diagnostics report tier + masks.
**Method-channel methods:** `getBeautyEffectiveQuality`, `getMaskPoolNames`.
**Files:** `jni_bridge.cpp` (`car_p3_beauty_effective_quality` → int;
`car_p3_mask_pool_list(outNames, maxNames)` → marshal `const char**` to a Kotlin
`List<String>`), `CommunityARPlugin.kt` (handlers).
**Notes:** `SkinSmoothEffect` installs through the **same** `car_p2_graph_set`
(typeId `8`, `CARBeautyFilterConfig` 60 B) — no new install path. Auto tier runs a
first-activation benchmark; expose the resolved tier via the diagnostic.
**Acceptance (compile):** builds. **On-device:** beauty visibly smooths on all
three skin tones (tone-awareness is automatic via `skinTone.baselineLuma` — never
a user knob, invariant 14); `getBeautyEffectiveQuality` returns High/Med/Low
after Auto resolves; `getMaskPoolNames` lists `masks.faceSkin` etc. Requires
WP-A (FaceMesh + face-skin mask via segmenter) + WP-B + WP-C plumbing.
**Risks:** the multiclass segmenter output-texture format assumption (M7 gate in
`car-phase-3-dag.md`) is unverified — validate the face-skin mask visually.

### WP-E — Phase 1 JNI + Kotlin: perception diagnostics + debug overlays
**Goal:** stats HUD, overlays, filter tuning, forced perception all work.
**Method-channel methods:** `getPerceptionStats`, `setDebugOverlay`,
`setOneEuroParams`, `forcePerception`.
**Files:** `jni_bridge.cpp` (`car_p1_get_perception_stats` → build the stats
`Map`; `car_p1_set_debug_overlay(mask)`; `car_p1_set_one_euro_params(min,beta,d)`;
`car_p1_force_perception(&CARPerceptionRequest)`), `CommunityARPlugin.kt`
(handlers; map the request `Map<String,int>` → the struct).
**Overlay rendering:** `debug_overlay.h::render(inputCamera, …)` draws overlays;
confirm it composites into the display path (likely a final overlay pass in
`renderFramePhase2` when `debugMask != 0`). If overlays aren't yet drawn in the
render loop, add that pass (small).
**Acceptance (compile):** builds. **On-device:** stats HUD populates
(faces/ms/luma); each overlay draws; One-Euro sliders visibly change jitter;
`forcePerception` makes an overlay show even with no effect installed. Requires
WP-A + WP-B.
**Risks:** stats readback must be non-blocking (invariant 5); skin-tone is 1–3
frames late by design (don't "fix").

---

## 5. Cross-cutting requirements

- **Threading:** all GL/native-render calls run on `GlRenderPipeline`'s GL thread
  (Option A). Graph mutations use `runOnRenderThread` (thread-safe queue). Stats/
  count/quality reads are safe from the platform thread (locks/atomics inside).
- **C ABI stability (invariant 2):** every new capability is a **new symbol**;
  never modify a shipped one. New struct fields append after a leading `version`.
- **Per-track state (invariant 4):** anything per-face keys off `faceId`; the
  engine already does — don't introduce per-slot state in the glue.
- **Orientation consistency:** exactly one place applies the camera UV transform
  (ingress, WP-B). Do not also apply it at present or you double-rotate.
- **Performance budget (33 ms @ 30 fps):** ingress copy + present blit ≈ 1–2 ms;
  perception 15–20 ms; effects 2–5 ms. Log frame time; justify >1 ms additions.
- **Diverse-face testing (invariant 10):** no AR effect is "done" until verified
  on dark/medium/light skin and ≥3 hair textures. Use `TESTING.md` checklists.
- **iOS:** out of scope here (design in [`IOS_RENDER_PIPELINE.md`](IOS_RENDER_PIPELINE.md));
  the C ABI + Dart are shared, so WP-C/D/E's native/Dart parts transfer, but the
  iOS platform glue (Swift + the pull-based present) is separate.

---

## 6. Verification & honesty boundary

| Layer | Tool | Proves |
|---|---|---|
| Native C++/JNI | NDK clang `-fsyntax-only` (`aarch64-linux-android24`, `-Inative/core -Inative/core/ffi`) | TU compiles |
| Whole Android build | `flutter build apk --debug` | Gradle→CMake→NDK link (needs vendored TFLite for WP-A+) |
| Dart | `dart analyze` | Dart compiles (mostly unchanged here) |
| **Runtime** | **physical device only** | models load, masks correct, effects correct, perf — **none provable in this dev env** |

State the "compiles vs works" boundary on every PR. Land one WP per PR
(conventional-commit title, DCO `-s`, `Co-Authored-By`); **sync `master` and cut
a fresh branch before each** (a merged PR can't take late commits — this has bitten
the project three times).

---

## 7. Suggested sequencing for the implementer

1. **WP-A** (TFLite + models + model dir) — nothing loads without it; needs a
   networked build machine for the `.so`s.
2. **WP-B** (ingress + present) — makes the AR render path exist without changing
   the visible result; safest to land and verify against the working Phase 0.
3. **WP-C** (effect graph → LipsEffect) — first visible AR; smallest new surface.
4. **WP-D** (beauty) — reuses C's graph path; adds 2 diagnostics.
5. **WP-E** (perception diagnostics + overlays) — best done last; it's the
   observability layer that also helps validate A–D on-device.

Each WP is independently compile-verifiable; only on-device testing crosses the
"works" line, and that's the maintainer's loop.
