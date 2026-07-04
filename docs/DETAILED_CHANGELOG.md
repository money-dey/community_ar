# Detailed Changelog — Decisions & Rationale

A dated record of the **design/implementation decisions** behind each change:
the options considered, why one was chosen and others rejected, and the related
commit + PR. This is the "why," complementing `CONSOLIDATION_AND_BRINGUP.md`
(the "what") and `START_HERE.md` (the entry point).

**Newest first. Append an entry for every non-trivial decision going forward.**

Entry format: `PR #N — title (date, commit) · Change · Options · Decision & why`.

---

## Cross-cutting decisions (apply throughout)

- **Consolidate the "delta document" pattern, don't extend it.** The repo shipped
  Phase-N changes as separate `*_phase*_updates.*` files that *described* edits to
  canonical headers/classes but were never merged — so compiled code referenced
  interfaces that existed only in comments. **Decision:** merge each load-bearing
  delta into its canonical file and delete it, rather than keep the pattern.
  *(Alternative rejected: keep writing deltas — it's what caused the non-compiling
  state.)*
- **Verify by layer; never conflate "compiles" with "works."** No device / TFLite
  / iOS in the dev env. **Decision:** verify native C++ with the Android NDK clang
  (`-fsyntax-only`), Dart with `dart analyze`, and the whole Android build with
  `flutter build apk --debug`; state the "not runtime-verified" boundary on every
  change. *(This caught an early over-claim — "Phase 0 is fully wired" — which was
  corrected once the pixel handoff was actually traced.)*
- **One focused fix per branch/PR**, conventional-commit title, DCO `-s`,
  `Co-Authored-By: Claude Opus 4.8`; the maintainer reviews/merges each; sync
  `master` before the next.

---

## 2026-07-04

### Third on-device iteration — the BlazeFace decoder was still a stub
- **On-device result after #32:** no black, models load (log confirms
  face_detector/landmarker/iris with the GPU delegate), graph installs at
  startup — but no effects and no overlays. Root causes:
  1. **`decodeBlazeFaceOutput` was STILL the scaffold stub returning `{}`
     unconditionally** — the exact gotcha CLAUDE.md warned about; the "Phase 1
     decoder fix" never actually landed. Zero faces was deterministic. Wrote
     the real decoder: SSD anchors for the short-range config (strides
     {8,16,16,16}, fixed_anchor_size → 896 anchors at 128², count verified
     against the model with a log), sigmoid + 0.5 threshold, greedy NMS
     (IoU 0.3).
  2. **The detection read assumed one 896×17 tensor**; the model emits TWO
     ([1,N,16] regressors + [1,N,1] scores) — the readOutput size check
     failed and the return value was ignored, so decode ran on zeros. Now
     shape-driven (outputs identified by channel count), both reads checked,
     log-once on failure.
  3. **Input range:** MediaPipe face_detection is trained on [-1,1] (FaceMesh/
     Iris on [0,1]). Added `CameraInputRect::signedInput`; the CPU-staged
     backend honors it; the detector sets it.
  4. **Debug overlays doing nothing is EXPECTED** — WP-E was never wired.
     Landed the first slice: `community_ar_phase1_api.cpp` **did not exist**
     (declarations only — another consolidation gap, invisible until the first
     reference failed to link). Implemented `car_p1_get_perception_stats` for
     real (atomic snapshot in the session, updated per AR frame → the HUD's
     `facesDetected` is now live) + JNI/Kotlin `getPerceptionStats`; the other
     three car_p1_* calls are accepted-but-inert with a log-once until full
     WP-E. Also: loadModel now logs each model's tensor shapes (one line) —
     the on-device instrument for layout assumptions.
- **Verification:** NDK clang on all changed TUs; `flutter build apk --debug`
  links (caught that the phase1 ABI had no implementation file — added to
  CMake). Detection quality/thresholds are device-verifiable only; the HUD
  faces count is the acceptance signal.

### Second on-device iteration — GLES blit() stub + startup graph race
- **On-device result after #31:** shader errors gone, but Beauty → black
  viewport; disabling Beauty (leaving Lips installed) restored the camera.
  That localization was decisive: the chain + lips render fine → the black is
  inside SkinSmooth's delivery path. Two root causes found by reading the
  path:
  1. **`GlesRenderContext::blit()` was an empty stub** ("implemented at the
     Session layer") — but it is load-bearing in SIX call sites: the effect
     graph's empty-graph camera copy, SkinSmooth's mid-band preservation,
     temporal-history seed/update, its **final output write**
     (`blit(scratch, outputFbo)`), and its no-face passthrough. The 9-pass
     pipeline computed everything and never delivered a pixel. Implemented as
     a lazy shader-based copy (passthrough program + fullscreen quad).
     Corollary: the black-on-faces path implies **FaceMesh was detecting**
     (hasFaces=true) — the model stack works.
  2. **Startup race:** `CommunityARView` pushes its initial graph during
     `initState`, before `createSession` completes → Kotlin returned
     invalid-session and Dart's graceful catch swallowed it → the app's
     default Beauty+Lips were never installed ("nothing changes" at startup;
     the first slider touch was the first successful install). Fix: the
     plugin stashes a pre-session graph and applies it in `createSession`;
     `clearEffectGraph` clears the stash.
- **Verification:** NDK clang + `flutter build apk --debug`. Runtime is
  device-only, as ever: expected result is smoothing/lipstick live at app
  start, no black on slider changes.

### First on-device AR run — GLSL reserved-keyword fix + missing multiclass model
- **On-device result (first run of the effect chain, from the #28+#30 branches):**
  no lipstick, and **black viewport with Beauty on**. Logcat pinpointed it:
  `L0003: Keyword 'input' is reserved` — the Oklab recolor fragment shader
  (masked_recolor_effect.cpp) used `input` as a variable name. `input` is a
  reserved keyword in GLSL ES; permissive drivers accept it, this device's
  compiler rejects the whole shader → recolor program never links → every
  frame throws GL_INVALID_OPERATION (the logged 0x502 spam).
- **Fixes:** (1) renamed `input`→`srcColor` (audited every inline shader for
  other reserved identifiers — `input`/`output`/`sample`/`filter`/`texture` —
  this was the only hit); (2) `GlesShaderProgram` now tracks link status:
  `use()` on an unlinked program logs ONCE and leaves the previous program
  bound instead of raising per-frame GL errors that drown other failures;
  (3) found the **default segmenter model was never fetched** —
  `segmenter_backend_factory.cpp` loads `selfie_multiclass_256x256.tflite`
  (source of beauty's face-skin mask); added it to `fetch_models.sh` and
  fetched locally (~16 MB float32, no float16 variant published — blows the
  old <15 MB budget; accepted for bring-up, slimming is a follow-up).
- **Verification:** NDK clang on both TUs; `flutter build apk --debug` on this
  branch (stub TFLite — the branch is off master; the model/TFLite bundling
  arrives when #30 merges). Shader correctness on the strict compiler is
  only provable on-device — retest needs #28 + #30 + this merged.

### WP-A complete — prebuilt TFLite (no Bazel) + CPU-staged I/O + model plumbing
- **Change:** perception can now actually run on-device (pending runtime
  verification). Four pieces:
  1. **`tools/fetch_tflite_prebuilt.sh`** vendors the official Maven Central
     2.16.1 AARs (runtime + GPU delegate) — headers + per-ABI `.so`s into
     `third_party/tensorflow-lite/`, plus copies into `android/src/main/jniLibs/`
     for APK packaging; fetches one header the AAR omits
     (`core/c/registration_external.h`). All gitignored.
  2. **`tflite_backend.cpp` reworked for CPU-staged tensor I/O** (the prebuilt
     binaries don't export the GL-interop `Bind*` symbols — verified with
     llvm-nm): the model-input crop is blitted on GPU (crop+resize+rotate+
     mirror in one `CropResizeBlitter` pass, now `sampler2D` since WP-B's
     ingress feeds it 2D), synchronously read back (≤256², documented
     invariant-5 rationale), and copied into the tensor as float [0,1];
     segmenter outputs upload back into the caller's mask texture inside
     `bindOutputTexture` (same caller contract). GL-interop path preserved
     behind `-DCAR_TFLITE_GL_INTEROP=ON` (explicit opt-in — the AAR ships the
     gl *headers* without the *symbols*, so directory auto-detection was wrong
     and is not used). Inference itself stays GPU-delegate-accelerated.
  3. **GLES `createFramebufferForTexture` implemented** — the pre-existing
     nullptr gap flagged in `render_context.h`; it is load-bearing for the
     effect-chain ping-pong FBOs, the mask rasterizer, and all beauty targets,
     i.e. effects could never have rendered without this.
  4. **Model plumbing:** models bundle as plugin assets
     (`build.gradle.kts` sourceSets → `native/models`), extracted to
     `filesDir/models` on first launch (`extractModelsIfNeeded`, matching the
     CLAUDE.md gotcha), path handed to native via **new symbol**
     `car_p0_set_model_directory` (the shipped `CARPhase0Config` layout is
     untouched per CLAUDE.md §2) → `BackendConfig.modelDirectory`.
- **Why prebuilt over Bazel:** decision + trade-off analysis in the 2026-07-03
  discussion — Bazel-from-source buys only the zero-copy GL binding; prebuilt
  gives the full C API + GPU inference with ~2–5 ms staging cost inside the
  3–13 ms budget headroom; reversible via the CMake option.
- **Bugs caught by layered verification:** (a) CMake `find_library` silently
  linked the stub even with libs vendored — NDK re-roots `PATHS` onto the
  sysroot + caches NOTFOUND; replaced with direct `EXISTS` checks, confirmed
  via `DT_NEEDED` inspection of the APK's `.so`. (b) the real backend never
  defined the `extern "C" tflite_backend_invalidate_frame` hook the perception
  pipeline links (only the stub did — this TU had never compiled before).
- **Verification:** backend TU compiles against the real vendored headers (a
  first); full `flutter build apk --debug` links; APK inspected —
  `libcommunity_ar_native.so` `DT_NEEDED`s both TFLite libs, 6 `.tflite`
  assets + per-ABI TFLite `.so`s packaged. **NOT runtime-verified:** model
  loading, inference, staging correctness, and performance all need the
  device.


## 2026-07-03


### WP-A (models half) — MediaPipe models fetched; fetch script repaired
- **Change:** all 6 MediaPipe models are now fetched locally into
  `native/models/` (~7.1 MB, under the 15 MB budget; all verified `TFL3`
  flatbuffers): face_detector, face_landmarker, face_blendshapes (extracted from
  the `.task` bundle), iris_landmark, hair_segmenter, selfie_segmenter. Fixed
  `tools/fetch_models.sh`: the iris model's GitHub-raw URL 404s (legacy modules
  removed from `master`) → now the `mediapipe-assets` bucket. Added a header
  note for the Windows/Git-Bash schannel `CRYPT_E_NO_REVOCATION_CHECK` failure
  (workaround: `ssl-no-revoke` via a temp `CURL_HOME`, which keeps chain
  validation on). Gitignored `native/models/*` artifacts (fetch-on-demand by
  design; binaries don't belong in git).
- **Remaining WP-A:** TFLite `.so` vendoring still needs Bazel (not present in
  this env) — per-ABI build commands are printed by `tools/fetch_tflite.sh`;
  then asset bundling/first-launch extraction + `modelDirectory` plumbing
  (compile-verifiable here once started).
- **Verification:** models downloaded + magic-checked locally; script change is
  the same URL that was verified working. Model *loading* is untestable until
  TFLite is vendored.

### WP-C — effect-graph method channel: setEffectGraph → car_p2_graph_set
- **Change:** implemented `AR_INTEGRATION_SPEC.md` WP-C. Kotlin now handles
  `setEffectGraph` (unpacks the Dart parallel arrays — `List<Int>` typeIds +
  `List<ByteArray>` configs — errors surface as `PlatformException`),
  `clearEffectGraph`, and `getEffectCount`. JNI `nativeSetEffectGraph` marshals
  to `car_p2_graph_set`'s C shape (both layers copy the config bytes, so Java
  refs release immediately); `nativeClearEffectGraph`/`nativeGetEffectCount`
  are thin. Also fixed a latent race: `car_p2_graph_effect_count` used the
  lazily-constructing `effectGraph()` accessor from the platform thread; added
  a non-constructing `Phase0Session::installedEffectCount()` peek (internal
  change, same ABI signature).
- **On-device consequence (important):** the example app installs Beauty+Lips
  by default, so merging this makes `renderFramePhase2` (perception with the
  **stub** neural backend — no TFLite/models until WP-A — plus the effect
  chain) run on-device for the first time. Expected: effects no-op with zero
  faces detected → passthrough camera. If the preview goes black or crashes
  with effects toggled ON but is fine with them OFF, the gap is in the
  no-face effect-chain passthrough — valuable signal, and the toggles are the
  escape hatch.
- **Verification:** NDK clang on 3 changed TUs + `flutter build apk --debug`.
  Not runtime-verified.


### WP-B — AR render path: camera ingress + renderFramePhase2 + present-blit
- **Change:** implemented `AR_INTEGRATION_SPEC.md` WP-B. New C ABI symbol
  `car_p2_submit_frame_display(session, oesTex, w, h, texMatrix16, timestampNs)`
  → `Phase0Session::submitFrameAR/processFrameAR`: (1) drain the render queue so
  graph installs take effect same-frame; (2) **ingress** — OES camera → 2D RGBA
  FBO applying the UV transform once (`cameraOutputTexture()` now returns this,
  replacing the invalid-handle stub, so perception/effects sample an upright,
  zoomed sampler2D frame); (3) graph installed → `renderFramePhase2()` into the
  offscreen `outputFbo_` then a passthrough **present-blit** to fbo 0; graph
  empty → present the ingress texture through the Phase 0 test-mode (2D)
  shaders. JNI `nativeSubmitFrameAr`; Kotlin `GlRenderPipeline` now calls it
  unconditionally with `SurfaceTexture.timestamp`.
- **Options:** (per spec §3c) new subsuming symbol vs. Kotlin branching on
  `getEffectCount()`. **Chose the subsuming symbol** — one entry point, the
  empty-graph path is pixel-identical to Phase 0 (test modes preserved), and
  Phase 0 symbols stay untouched (CLAUDE.md §2).
- **Design notes:** graph presence is a pointer check (`p2_->effectGraph &&
  effectCount()>0`), not the lazy accessor — the empty-graph hot path must not
  construct Phase 2 machinery. Orientation is applied exactly once (ingress);
  the present-blit is identity. Costs one full-frame copy + one blit (~1–2 ms,
  within budget).
- **Verification:** NDK clang on the 4 changed TUs + `flutter build apk --debug`
  link. **Not runtime-verified**; on-device acceptance = empty graph renders
  identical live camera (incl. test modes, zoom, orientation, resume). With no
  TFLite/models (WP-A) and no `setEffectGraph` handler (WP-C), the graph path is
  unreachable on-device — by design, this lands the plumbing safely first.

### AR feature integration spec (Phases 0–3) — docs
- **Change:** added `docs/AR_INTEGRATION_SPEC.md` — an implementation-ready spec
  for wiring the perception/effect features (which already compile in C++ and are
  exposed in Dart) through the Android platform layer. Contains a feature
  inventory table (phase → feature → Dart API → C ABI → model → status), the
  integration architecture (OES→2D ingress, orientation/zoom at ingress,
  `renderFramePhase2` + present-blit, model-directory plumbing), and ordered work
  packages WP-A…WP-E each with files/interface/acceptance. START_HERE links it as
  the next-work plan.
- **Why:** the maintainer is handing implementation to another model and needs a
  clean, self-contained spec. Grounded in the actual code: `car_p1/2/3_*` ABI,
  the `renderFramePhase2` path (`cameraOutputTexture()` currently returns an
  invalid handle; `neuralBackend()` has an empty `modelDirectory`), the 5 models
  in `fetch_models.sh`, and the effect struct layouts (Lips 28 B / Beauty 60 B).
- **Key finding recorded:** the render-loop reconciliation is the crux — the live
  Option A pipeline presents fbo 0, but `renderFramePhase2` renders to the
  offscreen `outputFbo_`; integration needs an OES→2D ingress (carrying the UV
  transform so perception sees an upright frame) and a present-blit back to fbo 0,
  via a new `car_p2_submit_frame_display` symbol (with a timestamp param for
  One-Euro/perception). Dart→channel side is already done; work is Kotlin+JNI+
  native-render+models.
- **Verification:** docs only.

### Camera lifecycle — resume live preview after backgrounding
- **Symptom:** leaving the app and returning left the preview frozen on the last
  frame. Cause: on background the OS disconnects Camera2 (our `onDisconnected`/
  `onError` just closes it) and nothing restarted it on resume, so the Flutter
  `Texture` kept showing the last swapped buffer.
- **Change:** `CommunityARPhase0View` now observes the app lifecycle
  (`WidgetsBindingObserver`): stop the camera on pause/inactive/hidden/detached,
  restart it on resume. New lightweight `stopCamera` method-channel call +
  `CommunityARPlugin.stopCamera()` that tears down only Camera2.
- **Why this shape:** the GL/EGL pipeline, native session, and the Flutter
  `SurfaceTexture` all survive backgrounding, so only Camera2 needs to be
  released/reopened — the texture id and pipeline are untouched, so resume just
  re-points a fresh capture at the same pipeline surface. Also releases the
  camera for other apps while backgrounded (good citizenship). Zoom resets to
  1.0 on resume (a fresh capture request starts un-zoomed).
- **Verification:** `dart analyze` (no new issues) + `flutter build apk --debug`.
  The background/resume behaviour itself is **only confirmable on-device**. If a
  device also invalidates the Flutter `SurfaceTexture`/EGL surface on background
  (resume shows black rather than frozen), a surface-recreate step would be the
  follow-up — not implemented since the reported symptom is a stale frame.

### Zoom — allow deeper zoom-out (below 1.0×)
- **Change:** expose the camera's minimum zoom (`getMinZoom`) and clamp the pinch
  to `[minZoom, maxZoom]` instead of `[1.0, maxZoom]`. On hardware-zoom devices
  with an ultra-wide lens, `CONTROL_ZOOM_RATIO_RANGE.lower` is < 1.0 (~0.5–0.6),
  so the preview can now zoom out to the wider field of view.
- **Why / scope:** the digital fallback stays floored at 1.0 — there is no image
  data beyond the sensor's full frame, so sub-1.0 digital zoom would only smear
  edges. "Deeper zoom out" is therefore a hardware-only capability, surfaced
  through the same single `setZoom()`/range API.
- **Verification:** `dart analyze` (no new issues) + `flutter build apk --debug`.
  The actual sub-1.0 range depends on the device's lens; only confirmable on an
  ultra-wide-capable device.

### Render-pipeline ownership ADR + iOS pipeline guide (docs)
- **Change:** added `docs/RENDER_PIPELINE_OWNERSHIP.md` (why the platform layer,
  not C++, owns the GPU context + presentation surface — an ADR with the reframe,
  decision factors, the concrete challenges/tradeoffs hit implementing Option A on
  Android, and a reversibility analysis) and `docs/IOS_RENDER_PIPELINE.md` (the
  iOS Metal analogue of the Android bring-up guide). START_HERE links both.
- **Why:** the ownership choice is load-bearing (shapes the FFI boundary,
  threading, cross-platform symmetry, debuggability) and the maintainer asked for
  it to be written down. The iOS guide captures — before anyone builds iOS — that
  it has the *same class* of black-preview bug as Android but a **different** fix:
  iOS is pull-based (`copyPixelBuffer`), renders into a private-storage
  `MTLTexture` that's never connected to the fresh/blank `CVPixelBuffer` handed to
  Flutter, so the fix is "render into the IOSurface-backed texture Flutter reads,"
  not "render to fbo 0 + swap."
- **Decision recorded:** **Option A (platform owns context/surface)** on both
  platforms — chiefly for iOS symmetry (Swift necessarily owns `MTLDevice` +
  `FlutterTexture` + `AVCaptureSession`), because Option B would *increase* the
  JNI/FFI surface (Camera2/Surface are Java APIs), and because on-device bring-up
  tooling is platform-side. Reversible: the C ABI already treats the context as
  externally owned.
- **Verification:** docs only; no code change.

### Pinch-to-zoom — hybrid hardware + digital (utility feature)
- **Change:** pinch-to-zoom on the camera preview. Gesture is a Flutter
  `GestureDetector` (scale) over the Phase 0 view → `setZoom(factor)` over the
  method channel; `getMaxZoom()` reports the active camera's ceiling. One Kotlin
  `setZoom()` routes to **hardware** zoom (`CONTROL_ZOOM_RATIO`, API 30+) when the
  camera advertises `CONTROL_ZOOM_RATIO_RANGE`, else a **digital** crop folded
  into the GL pipeline's UV transform (scale-about-centre by 1/zoom).
- **Options (put to the user):** digital-only (GPU crop); hardware-only
  (Camera2); **hybrid**. User chose **hybrid** — hardware quality where
  available, universal fallback everywhere.
- **Design notes:** no C++/C-ABI change — digital zoom rides the existing
  `computeUvTransform` (uniform scale, commutes with the rotation), hardware zoom
  is pure Camera2 (keep the preview `CaptureRequest.Builder` around, re-submit on
  zoom). Backend chosen per-camera from `CameraCharacteristics`, resolved
  synchronously when the camera opens so `getMaxZoom()` is ready right after
  `startCamera`. Zoom resets to 1.0 on camera switch (range/backend differ).
  Pixels stay on the GPU (CLAUDE.md §1) — Dart only sends a float.
- **Verification:** `dart analyze` (no new issues) + `flutter build apk --debug`
  build & link. Hardware-vs-digital selection and the actual zoom quality are
  **only confirmable on-device** (the dev env has no camera).

### Android pipeline — first on-device result + orientation tuning
- **Change:** the pipeline **shows live camera on a real device** (EGL,
  presentation into the Flutter texture, and OES sampling all confirmed working
  on hardware — the core bring-up milestone). First-pass image was a quarter-turn
  off ("lying on its side") and stretched. Reworked `GlRenderPipeline`'s
  `computeUvTransform` to a single fixed rotation (`UV_ROTATION_DEG`) + front
  mirror (`MIRROR_FRONT`), dropping the `SENSOR_ORIENTATION`-derived rotation;
  added a one-shot diagnostic logcat line.
- **Why / options:** on-device data showed **both cameras need the same
  quarter-turn** (only the front differs by a mirror), so a fixed rotation is
  correct and avoids the sign/handedness ambiguity of deriving it from
  `SENSOR_ORIENTATION`. Crucially, rotation and "stretch" are the *same* bug: a
  correct 90°/270° rotation swaps the `1280×720`↔`720×1280` axes and removes the
  stretch. Exact rotation value is device-dependent → exposed as a tuning knob to
  cycle `{270,90,0,180}` rather than guessing blind (the dev env can't see the
  screen).
- **Deferred:** the example app isn't portrait-locked; following live device
  rotation (display rotation + landscape buffer-dim swap) is a tracked follow-up.
  Portrait-upright is the first target.
- **Verification:** `flutter build apk --debug` builds & links. Orientation
  correctness itself is **only confirmable on-device** — the knob may need one
  cycle to land upright.

### Android GL/EGL pipeline — implement Option A (Kotlin owns EGL)
- **Change:** built the Android render pipeline that was only *documented* in
  PR #19. Kotlin now owns a dedicated GL render thread + EGL14 context + an EGL
  window surface built from the Flutter `SurfaceTexture`; the camera OES texture
  lives on that same context; each frame does
  `updateTexImage → compose UV transform → native render into fbo 0 → eglSwapBuffers`.
  New `GlRenderPipeline.kt`; `CameraStream.kt` reduced to pure Camera2-into-a-
  Surface (no GL); `CommunityARPlugin.kt` orchestrates. Native gained a display
  present path: new C ABI symbol `car_p0_submit_frame_display` (+ `texMatrix`),
  `Phase0Session::submitFrameToDisplay/processFrameToDisplay`, four
  `samplerExternalOES` shaders with a `uTexMatrix`, `RenderContext::
  bindDisplayFramebuffer` (GLES binds fbo 0 + viewport), and JNI
  `nativeSubmitFrameDisplay`.
- **Options:** (a) **Kotlin owns EGL** (Option A); (b) native (C++) owns EGL
  (Option B); (c) keep documenting.
- **Decision & why:** **(a)**, per the user's pick and the PR #19 recommendation.
  Standard Flutter-plugin pattern, EGL centralized where Android/GPU tooling can
  debug it, and the native side changed minimally (just render to fbo 0 instead
  of the offscreen FBO). This fixes all five documented defects: EGL context now
  exists; native presents into the Flutter texture via swap; the camera OES
  texture is created on the *same* context native renders on (valid handle); the
  display shaders sample `samplerExternalOES`; and the `SurfaceTexture` transform
  is applied to the UVs.
- **C ABI:** added a **new** symbol rather than changing `car_p0_submit_frame`
  (stability invariant, CLAUDE.md §2). The old offscreen path is untouched.
- **Deliberately deferred / needs a device:** exact rotation/mirror handedness in
  `computeUvTransform` and portrait vs. landscape sizing (fixed 720×1280 for now)
  are device-dependent — the mechanism is in place but the constants need the
  on-device checklist (ANDROID_RENDER_PIPELINE.md §4–5).
- **Verification:** NDK clang `-fsyntax-only` on all four changed native TUs +
  the JNI bridge; `flutter build apk --debug` builds & links the full Kotlin +
  native library. **NOT runtime-verified** — no device/GPU/camera here; the
  §5 on-device checklist (context creates → frames arrive → updateTexImage →
  draw+swap → widget shows camera → orientation) is the remaining work.

### PR #20 — START_HERE + session-handoff (docs, `b212e05`)
- **Change:** `docs/START_HERE.md` single entry point + README link; plus
  project memory files for auto-recall in new sessions.
- **Options:** (a) memory files only; (b) repo doc only; (c) both + entry point.
- **Decision & why:** **(c).** Memory files auto-load in a new Claude Code session
  but are concise hooks; a repo doc is durable and detailed; START_HERE ties them
  together so README → START_HERE → the right doc → the next task.

### PR #19 — Android render pipeline: document, don't implement (docs, `7582009`)
- **Change:** `docs/ANDROID_RENDER_PIPELINE.md` — design + on-device bring-up guide
  for the black-preview blocker.
- **Options (put to the user):** implement it with **Kotlin owning EGL**;
  implement with **native (C++) owning EGL**; **reconsider scope / document first.**
- **Decision & why:** user chose **document first.** Investigation showed the whole
  Android GL/EGL pipeline is unbuilt (no EGL context, no window-surface
  presentation, cross-context OES handle, `sampler2D` for an external-OES texture)
  — a large, 100%-unverifiable-here change. A precise design + checklist a
  contributor can drive on-device beats ~400 lines of unrunnable EGL. The doc
  still **recommends Kotlin-owns-EGL** (standard pattern, debuggable, minimal
  native change) for whoever implements it.

### PR #18 — Effect-graph channel graceful degradation (`da98150`)
- **Change:** `CommunityARView` swallows `MissingPluginException` from
  `setEffectGraph` so it degrades to passthrough instead of crash-spamming.
- **Options:** (a) swallow the exception (stopgap); (b) implement the Phase 2/3
  method-channel + JNI wiring now.
- **Decision & why:** **(a).** The full wiring is a large effort that also needs
  the render loop + texture handoff to be visible; a one-line resilience fix stops
  the immediate log-flood without pretending effects work.

---

## 2026-07-02

### PR #17 — Camera permission, natively (`20cd9a6`) — supersedes #16
- **Change:** request `CAMERA` via a plugin-native `ActivityAware` +
  `RequestPermissionsResultListener` (method-channel `requestCameraPermission`);
  keep the manifest declarations.
- **Options:** (a) `permission_handler` package (what #16 did); (b) native
  `ActivityAware` request; (c) manifest-only.
- **Decision & why:** **(b).** #16's `permission_handler` **broke the build** —
  it pulls a new AGP artifact that this machine can't download (`PKIX path
  building failed` SSL error). `androidx.core` (`ContextCompat`/`ActivityCompat`)
  is already on the classpath, so the native path needs **no new downloads**.
  **Lesson recorded (now a standing constraint): do not add new pub/Gradle
  dependencies in this environment.** Verified: `flutter build apk` succeeds.

### PR #16 — Camera permission via permission_handler (`5f65765`) — REVERTED by #17
- **Change:** added `permission_handler` + a Dart permission gate.
- **Why rejected:** correct in intent, but the new Gradle dependency failed to
  download on the dev machine's cert store, breaking the build. See #17.

### PR #15 — Make TensorFlow Lite optional (`3ef4f2b`)
- **Change:** build links a **stub** `tflite_backend` when TFLite isn't vendored,
  so the app builds without it; the real backend links automatically when present.
- **Options:** (a) keep #12's `FATAL_ERROR` when TFLite absent; (b) optional stub;
  and for (b): a separate stub file vs an `#if` guard inside `tflite_backend.cpp`.
- **Decision & why:** **(b) with a separate `tflite_backend_stub.cpp`.** #12's hard
  error blocked *every* build including the Phase 0 pipeline the user wanted to
  test, while a full TFLite build is a heavy device-bring-up task. Separate stub
  file avoids wrapping the large real file in macros. Verified: **first installable
  APK** built.

### PR #14 — Consolidation & bring-up log (docs, `118ab93`)
- **Change:** `docs/CONSOLIDATION_AND_BRINGUP.md`. **Decision:** a durable repo
  status doc (vs chat-only summary) so the state survives sessions.

### PR #13 — Bind segmenter output to a GPU texture (`2732026`)
- **Change:** `SegmenterImpl` binds the model output tensor to the GPU mask
  texture instead of the commented-out CPU readback that returned an
  uninitialized texture.
- **Options:** (a) CPU readback + re-upload; (b) `bindOutputTexture` (zero-copy).
- **Decision & why:** **(b)** — matches the multiclass backend and invariant 1
  (pixels stay GPU-resident). Prompted by an honest review of whether the
  segmenters would actually work (they wouldn't have, with the stub).

### PR #12 — TFLite vendoring scaffold (`50e6a15`)
- **Change:** CMake consumes `third_party/tensorflow-lite/{include,lib/<ABI>}`;
  `tools/fetch_tflite.sh` + README document how to obtain them.
- **Options:** (a) vendored prebuilt via a fetch script (source checkout for
  headers + from-source `.so` build); (b) Gradle `prefab` / Maven AAR.
- **Decision & why:** **(a).** `tflite_backend.cpp` uses the GPU **GL-interop**
  delegate API (`delegates/gpu/gl/*`, `TfLiteGpuDelegateBindGl*`), which is **not
  in the standard AAR** — only in the TF source tree. So a source checkout for
  headers + Bazel build for libs is required; a Maven AAR can't satisfy it.

### PR #11 — Consolidate the GLES render-context delta (`1f6f443`)
- **Change:** merge `gles_render_context_phase3_updates.cpp` into
  `gles_render_context.cpp`; add a `GlesFramebuffer` FBO-adopt constructor; delete
  the delta + its CMake entry.
- **Options:** (a) just rename `GLESRenderContext`→`GlesRenderContext`; (b) merge
  the methods into the canonical `.cpp`.
- **Decision & why:** **(b).** The delta defined members of a class it couldn't
  see (the class lives only in the other `.cpp`), so a rename alone can't compile
  — the members must live in the same TU. The MRT path also needed a
  framebuffer-adopt constructor that didn't exist.

### PR #10 — Two `gles_compute.cpp` build errors (`7333124`)
- **Change:** add `<GLES2/gl2ext.h>` (`GL_TEXTURE_EXTERNAL_OES`); make `mapped_`
  `mutable` (assigned in a `const` method). **Decision:** conformance fixes, no
  alternatives. **Discovery here:** the NDK sysroot ships GLES/EGL headers, so
  the GLES backend files *are* compile-verifiable — which unlocked #11.

### PR #9 — Flutter plugin + example platform scaffolding (`c5174d5`)
- **Change:** generate the missing plugin build files + `example/android` &
  `example/ios` runners; wire the C++ `CMakeLists.txt` into Gradle
  `externalNativeBuild`.
- **Options:** (a) hand-write the build files; (b) `flutter create` + reconcile.
  And for package name: keep `dev.communityar` vs adopt the conventional
  `dev.communityar.community_ar`.
- **Decision & why:** **(b)**, keeping `dev.communityar`. `flutter create`
  produces correct interconnected Gradle/Xcode files; hand-writing risks version
  drift. The package stays `dev.communityar` because the JNI symbols are
  `Java_dev_communityar_*` and the Kotlin already lives there. Verified via a real
  `flutter build apk` — which **surfaced the `gles_compute.cpp` bugs** (#10).

---

## 2026-07-01

### PR #8 — Example: showcase the full public API (`b8ed592`)
- **Change:** rewrite `example/lib/main.dart` into a comprehensive device-test
  harness (all presets, quality tiers, every beauty/lip param, debug overlays,
  stats HUD).
- **Options:** show a representative subset vs every exported capability; and
  keep deprecated `withOpacity`/`Color.value` vs the newer `withValues`/`.r`.
- **Decision & why:** show **everything** (it's the on-device test harness), and
  **keep the deprecated APIs** — their replacements require Flutter 3.27+ while the
  package targets 3.0+.

### PR #7 — Reconstruct the Phase 2 session integration (`76f9f91`) → 25/25
- **Change:** add the whole `Phase0Session` Phase 2 surface
  (`Phase2Members`/`p2_`, `effectGraph()`/`perceptionPipeline()`/`neuralBackend()`
  /render accessors), two `CARStatus` codes, and fix a move-only-lambda-in-
  `std::function` bug.
- **Options (put to the user):** **bounded** (just the FFI declarations, 24/25);
  **full** reconstruction (25/25); **stop + tracking issue.**
- **Decision & why:** user chose **full.** New `CARStatus` values were **added**
  (unused enum slots), never modifying existing ones (C ABI invariant 2). The
  effect list is move-only, so it's wrapped in a `shared_ptr` to stay copyable for
  `std::function`. All 25 platform-agnostic TUs now compile.

### PR #6 — SkinToneEstimator readback polling → member (`c7a3d55`)
- **Change:** make `pollPending` a member of the private `Impl` it touches.
- **Options:** (a) member function; (b) `friend`; (c) expose `Impl` publicly.
- **Decision & why:** **(a)** — it only touches `Impl` state; keeps encapsulation
  (no public `Impl`).

### PR #5 — Reconstruct the PerceptionPipeline Phase 3 integration (`efaad30`)
- **Change:** define the missing `Impl` (shared header), constructor/destructor/
  `unloadIdleModels`, unify two contradictory state models through `impl_->`, add
  a `RenderContext*` to the constructor, wire `run()`→`runSegmenterForFrame()`.
- **Options (put to the user):** **minimal** compile+link; **full** runtime
  reconstruction; **defer.**
- **Decision & why:** user chose **full.** No ground-truth to recover from (unlike
  the segmenter/TFLite case), so this is genuine reconstruction — flagged as
  compile/link-correct but not runtime-verified. `ctx` provisioning added to the
  constructor (vs a later setter) since `run()` needs it.

### PR #4 — Align segmenter backends to the canonical NeuralBackend API (`d16bc12`)
- **Change:** rewrite the segmenter backends + fix `perception_models.cpp` to use
  `loadModel()→NeuralModel` / `setInputTexture(CameraInputRect)`.
- **Options:** (a) align the consumers to the real interface; (b) change the
  `NeuralBackend` interface to match the consumers.
- **Decision & why:** **(a)** — **no genuine fork:** `TfliteBackend`/`CoreMLBackend`
  already implement the canonical interface, so the hallucinated
  `runInference`/`ModelConfig`/`unloadModel` consumers were simply wrong.

### PR #3 — `pnp_solver.cpp` missing `<vector>` (`9c36c65`)
- **Change/Decision:** add the include. Conformance; no alternatives.

### PR #2 — FaceTracker header compiles (`5756983`)
- **Change:** replace the `Config{}` in-class default argument (which consumed a
  nested type's default member initializers — ill-formed) with two constructors;
  add missing `<memory>`/`<functional>`.
- **Options:** braces vs parens for the default arg (neither fixes it) vs two
  constructors.
- **Decision & why:** **two constructors** — the only correct fix; the
  default-constructed `Config` moves to the `.cpp` where DMIs are usable.

### PR #1 — Consolidate the Phase 3 interface deltas (`a84f91a`)
- **Change:** merge the Phase 3 deltas into canonical headers — define
  `PerceptionInputs` for real, add the `Effect` `passOrder()`/`maskRequirements()`
  + mask-pool overloads, add `PerceptionFrame::segmentationMasks`, fold
  `RenderContextEx` into base `RenderContext`, fix `blit`.
- **Options:** keep Phase 2 `prepare/render` **pure** vs **non-pure**; keep
  `RenderContextEx` as a subclass (with casts) vs fold into base.
- **Decision & why:** made the Phase 2 methods **non-pure** so effects overriding
  only the pool overloads (`SkinSmoothEffect`) stay concrete; **folded
  `RenderContextEx` into base** and kept `using RenderContextEx = RenderContext`
  for backward-compatible call sites. Result: the effect graph + 9-pass beauty
  pipeline compile.
