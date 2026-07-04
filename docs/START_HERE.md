# START HERE

Single entry point for anyone — a new contributor, you resuming after a break,
or a fresh Claude Code session — picking up Community AR. Read this first, then
follow the links in order.

---

## 30-second state (2026-07-04)

Community AR is a Flutter face-AR plugin: a thin Dart API over a large
C++/GLES/Metal core. It arrived **documented as Phases 0–3 complete but not
building**; it has since been consolidated, built, and brought up on a real
device through four on-device debug iterations.

- ✅ **Compiles / builds / runs:** all platform-agnostic C++ TUs + Android
  GLES/JNI; `flutter build apk --debug` produces an installable APK.
- ✅ **Camera preview verified on hardware** (PRs #22–#25): live, upright,
  un-mirrored, pinch-zoomable, survives backgrounding. Kotlin owns the GL
  thread/EGL context (Option A, `GlRenderPipeline.kt`).
- ✅ **AR path wired end-to-end** (PRs #27–#30, per
  [`AR_INTEGRATION_SPEC.md`](AR_INTEGRATION_SPEC.md)): prebuilt TFLite AARs
  (no Bazel; CPU-staged I/O), 7 models bundled + extracted on first launch,
  OES→2D ingress → `renderFramePhase2` → present-blit, `setEffectGraph`
  method channel, perception-stats HUD (`faces:` count is the acceptance
  instrument).
- ✅ **Four on-device debug iterations done** (PRs #31–#34): GLSL reserved
  keyword broke recolor on strict drivers; `blit()` was an empty stub (beauty
  delivered no pixels) + startup graph race; BlazeFace decoder was still the
  scaffold stub → real SSD decode (`community_ar_phase1_api.cpp` created);
  rasterizer VBO/shader extended-API stubs crashed on first detection →
  implemented. **FaceMesh detects faces on-device.**
- 🟡 **Open: PR #35** — landmark/iris coords are input-PIXEL units, not [0,1]
  (found by audit): FaceMesh + iris outputs now normalized by the model input
  size from the tensor spec; iris output tensor identified by shape ([1,15])
  with checked reads (was silently processing zeros). **Next on-device run
  should show lipstick + smoothing on the face.**
- ⛔ **Still pending:** on-device iteration 5 (verify effects land correctly),
  perception-quality pass (lip placement, mask edges, diverse faces per
  `TESTING.md`), WP-D (beauty controls), rest of WP-E (debug overlays), live
  device-rotation follow-up, iOS (design only, needs a Mac).

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
1. ✅ **AR integration WP-A/B/C — DONE** (PRs #27–#30, spec:
   [`AR_INTEGRATION_SPEC.md`](AR_INTEGRATION_SPEC.md)): prebuilt TFLite + model
   bundling/extraction, AR render path (`car_p2_submit_frame_display`), effect
   graph channel. Plus on-device debug iterations 1–4 (PRs #31–#34): recolor
   shader fix, `blit()` + graph race, BlazeFace decoder, rasterizer VBO crash.
2. **On-device iteration 5** — merge PR #35 (landmark/iris pixel-unit scaling)
   + PR #36 (WP-E debug overlays, stacked on #35), rebuild, run on device.
   **Debug-first this time:** with NO effect installed, toggle the Landmarks
   overlay → ~478 green dots should track the face (directly shows where
   landmarks land — the observability we lacked). Then Iris (blue circles),
   HairMask (green tint on hair), Pose (RGB axis dots), One-Euro sliders
   (dot jitter visibly changes). Then install lipstick and check placement
   against the dots. Older techniques that still work: HUD `faces:` count,
   llvm-symbolizer against the unstripped
   `example/build/.../obj/arm64-v8a/libcommunity_ar_native.so`, loadModel
   tensor-shape log lines.
3. **Perception-quality pass** — lip placement, mask edges, iris radii, then
   the diverse-face checklist in [`../TESTING.md`](../TESTING.md).
4. **WP-D (beauty controls) + rest of WP-E** (per-model inference-ms stats
   are still zero in the HUD) from the spec.
5. **Live device-rotation follow-up**
   ([`ANDROID_RENDER_PIPELINE.md`](ANDROID_RENDER_PIPELINE.md) §4); iOS bring-up
   (needs a Mac).

---

## Dev environment — what can and can't be verified here

- **No Android device, no camera/GPU runtime, no macOS/Xcode.** GL/EGL/Metal/JNI
  and perception are **not runtime-verifiable**; iOS can't be built.
- **TFLite is vendored from prebuilt Maven AARs** (PR #30,
  `tools/fetch_tflite_prebuilt.sh` — no Bazel needed). The AARs lack the
  GL-interop API, so the backend uses CPU-staged tensor I/O (GL interop stays
  behind `-DCAR_TFLITE_GL_INTEROP=ON`). A stub backend still links if the AARs
  are absent. Models live in gitignored `native/models/`
  (`tools/fetch_models.sh`; direct `curl` works with the `ssl-no-revoke`
  workaround).
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
