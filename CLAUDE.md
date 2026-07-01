# CLAUDE.md

Operating manual for AI coding agents (and humans) working on this codebase.

This document captures the non-obvious decisions, invariants, and conventions that make Community AR what it is. The codebase has a strong opinion about how things should be done. Following these conventions is what keeps the architecture coherent across contributors.

---

## Mental model in one paragraph

Community AR is a thin Flutter API over a substantial C++ engine. Most of the actual work — perception, mask rasterization, shader-based effects, GPU resource management — happens in C++. Dart's job is to express *what* the user wants (a graph of effects with parameters); C++'s job is to execute that efficiently against camera frames using MediaPipe perception models and our own GLES/Metal renderers. Pixels never enter Dart. The C ABI is the stability boundary.

---

## Read these first if you're new to this repo

Before planning any non-trivial change, read:

- **[`docs/STRATEGY.md`](docs/STRATEGY.md)** — what this project is competing on. Not marketing; a technical strategy document that affects scoping decisions. The short version: the differentiator is cross-skin-tone correctness and adjacent under-served areas, not feature parity with Snapchat. When in doubt about whether to invest in a feature vs. deepen the differentiator, this doc is where the reasoning lives.

- **[`docs/CARRIED_FORWARD.md`](docs/CARRIED_FORWARD.md)** — deferred technical items with concrete "when to do this" conditions. If you're working in a phase, check whether any carried-forward items belong to that phase. Don't rediscover deferrals we already made.

- **[`TESTING.md`](TESTING.md)** — testing posture. Tests are part of the contribution, not separate from it. Read before submitting any PR that touches render pipeline code.

- Whichever phase document is relevant to your work: `docs/car-phase-0.md` through `docs/car-phase-3.md` for shipped phases; `docs/car-phase-n-requirements.md` for planned phases.

---

## Architectural invariants (do not violate these)

These are decisions that *everything else* depends on. Changing them requires a discussion, not a PR.

### 1. Pixels never enter Dart.
Camera frames, masks, intermediate textures, and the final composited output all stay GPU-resident from capture to display. Dart sees texture IDs, configuration structs, and event callbacks — never raw image data. If a code path requires reading pixels into Dart memory, it's almost certainly wrong; flag it.

### 2. The C ABI is the stability boundary.
Anything in `native/core/ffi/` is stability-bound forever once shipped. Struct layouts, function signatures, enum values: don't change them. New features add new symbols; never modify old ones. Versioning is per-struct via a leading `uint32_t version` field.

### 3. Perception runs on demand, not always-on.
Each effect declares its `PerceptionInputs` requirements; the `EffectGraph` computes the union; the `PerceptionPipeline` only runs the models that are actually needed. Adding a perception module that runs unconditionally is wrong. If you add a model, it must be gated on a `PerceptionInputs` flag.

### 4. Per-track state, not per-slot state.
Filters, motion estimates, skin tone, iris state — all keyed by stable `faceId` from `FaceTracker`. If you add per-face state, key it by `faceId` and clean it up via the `retainOnly()` pattern. Per-slot state (indexed by array position) is a bug waiting to happen because slots reorder when faces enter/leave the frame.

### 5. GPU readback is asynchronous.
Don't block the render thread waiting for GPU results. Use the `AsyncReadback` polling pattern (see `skin_tone.cpp` for the reference implementation). A synchronous readback is acceptable only with a written rationale in code comments.

### 6. One-Euro filter on all landmark outputs.
Raw model output has sub-pixel jitter on every landmark every frame. Without temporal smoothing, the entire pipeline looks janky regardless of how good everything else is. If you add a new landmark type, wrap it in `OneEuroFilter2D` (or the array variant) per-track. The defaults `minCutoff=1.0, beta=0.007` work for 30fps face landmarks; iris uses `minCutoff=1.5, beta=0.01`.

### 7. Three effect engines, not per-feature implementations.
Adding "TeethWhitening" is a 10-line factory function that pre-fills `MaskedRecolorEffect::Config`, not a new class. Adding "EyeEnlarge" is a config for the (future) `LandmarkWarp` engine. If you're tempted to write a new engine, ask whether the engine should be generalized further first.

### 8. Oklab for color math.
All recolor blending happens in Oklab space because RGB blending fails on diverse skin tones. If you write a new shader that touches color, convert to Oklab, do the math, convert back. Helpers are inlined in `masked_recolor_effect.cpp`; refactor them into a shared header when you need them in a second place.

### 9. No Eigen, no OpenCV.
Custom inline numerics for the few things we need (PnP solver, image math). Keeps the binary lean and avoids dependency hell. If you genuinely need linear algebra beyond what `pnp_solver.cpp` provides, add a small header to `native/core/math/` rather than pulling in Eigen.

### 10. Real device testing on diverse faces is part of "done."
A new effect isn't ready to merge until it's been tested on dark, medium, and light skin tones and at least three different hair textures. The verification checklists in `docs/car-phase-*.md` are not suggestions. **Comprehensive testing procedure documented in [`TESTING.md`](TESTING.md) at the repo root — read it before submitting changes that touch the render pipeline.**

### 11. Effect ordering goes through `passOrder`, not declaration order.
Phase 3 introduced the `EffectPass` enum: `BaseColorGrade < SkinAdjust < Recolor < Warp < Overlay < Background < PostProcess`. Effects sort by `passOrder()` value before running. Don't rely on the order the user wrote them in. Same-`passOrder` effects keep declaration order (stable sort). Gap-numbered: pick an integer in the gap when adding a new category, don't renumber.

### 12. Mask resources have canonical names.
The `MaskResourcePool` is keyed by string constants exported from `MaskResourcePool::kFaceSkin`, `kHair`, `kBodySkin`, `kClothes`, `kBackground`, `kLipsContour`, `kFaceLandmarkSkin`. Define new mask names there, not as inline string literals. Mask pool clears each frame; producers re-populate; consumers read; nothing crosses frames.

### 13. Effects declare what they need from perception AND from the mask pool.
`perceptionInputs()` returns OR-able flags for what the segmenter / landmarker / pose / skin-tone needs to run this frame. `maskRequirements()` returns `{consumes, produces}` for the mask pool. Graph reads the union and configures both layers. A new effect that needs face-skin mask flips `needsFaceSkinMask=true` AND adds `masks.faceSkin` to its consumes list — both, not either.

### 14. Beauty quality tiers auto-resolve; respect the user's explicit choice.
`SkinSmoothEffect` runs an auto-tier benchmark on first activation only when `cfg_.quality == BeautyQuality::Auto`. If the user explicitly picked High/Medium/Low, do not run the benchmark and do not adaptively throttle. Tone-awareness is automatic (driven by `face.skinTone.baselineLuma`) — never expose it as a user-facing knob.

---

## Code organization

```
native/core/
├── ffi/                    Stability-bound C ABI. Don't break.
├── perception/             MediaPipe wrappers, filters, trackers, pose
├── math/                   Custom numerics (PnP, etc.)
├── ml/                     Neural inference backends (TFLite, Core ML)
├── render/                 RenderContext abstraction + GLES/Metal impls
├── effects/                The three engines + effect factories
└── shaders/                GLSL ES sources (currently inline in C++)

lib/                        Public Dart API
├── community_ar.dart       The single import users need
└── src/
    ├── effects/            Dart-side Effect base + EffectGraph + LipsEffect etc.
    ├── ffi/                Method channel surfaces
    └── widgets/            CommunityARView

android/                    Kotlin platform adapter + JNI bridge
ios/                        Swift platform adapter

native/models/              MediaPipe models (downloaded via tools/fetch_models.sh)
tools/                      Build scripts (fetch_models.sh, convert_models_to_coreml.py)
docs/                       Phase-by-phase architecture writeups
example/                    Runnable demo app
```

---

## How to add a new MaskedRecolor effect (the common case)

This is the most common kind of contribution we expect. Reference example: `lips_effect.cpp`.

1. **Find the FaceMesh landmark indices** for the contour you want to recolor. Use [MediaPipe's canonical face mesh visualization](https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/modules/face_geometry/data/canonical_face_model_uv_visualization.png).
2. **Pick the next free effect type ID** from `effect_types.h`. Don't reuse retired IDs.
3. **Define the config struct** in `community_ar_phase2_api.h` (or a new `phaseN_api.h` if you're past Phase 2). Always lead with `uint32_t version`.
4. **Write the factory function.** Copy `lips_effect.cpp`, change the contour indices, the type ID, and any blendshape gating logic. Total file size should be under 100 lines.
5. **Register the factory** in `community_ar_phase2_api.cpp`'s `factoryForType()` switch.
6. **Write the Dart class.** Copy `lips_effect.dart` (Dart side), change the typeId, change the parameter set, match the C struct layout in `serialize()`.
7. **Add the export** to `lib/community_ar.dart`.
8. **Test it.** On diverse faces. Run the verification checklist from `docs/car-phase-2.md` adapted for your effect.

If you find yourself writing more than ~100 lines of C++ for a new MaskedRecolor effect, you're probably doing too much in the factory. The engine should handle it.

---

## Coding conventions

### C++

- C++17. `#pragma once` headers. No exceptions in render-thread paths.
- Naming: `PascalCase` for classes, `camelCase` for methods, `snake_case` for file names. `kConstantName` for constexpr.
- One class per file unless they're tightly coupled internals (e.g. `Impl` pimpls).
- Comments at the top of each file explain *why* the file exists and what design tensions it resolved. We deliberately do "literate programming" style commentary — it's slow to write and fast to read, which is the right trade-off for a codebase meant to be modified by contributors.
- `std::unique_ptr` for owned resources. Avoid `std::shared_ptr` unless ownership is genuinely shared (rare).
- Logging: `__android_log_print` on Android, `os_log` on iOS. Don't use `printf` or `std::cout` in shipped code.

### GLSL

- GLSL ES 3.00 for fragment, GLSL ES 3.10 for compute. `precision mediump float` unless precision matters.
- Inline shader sources in C++ string literals for now. SPIRV-Cross unification (sharing one source between GLES and Metal) lands in Phase 5; until then, MSL counterparts live alongside the GLSL in the same file when the platform needs them.
- Document non-obvious math in comments. The Oklab conversion functions in `masked_recolor_effect.cpp` cite Björn Ottosson's reference; do the same.

### Dart

- `dart format` on every commit.
- Effects are immutable. If a user wants to change a parameter, they make a new effect.
- `EffectGraph` equality is by serialized-byte equivalence; if you add a new effect type, make sure `serialize()` is deterministic (no `Map<String, dynamic>` ordering surprises).

---

## Gotchas and surprises

These are real things that bit a previous contributor (or me). Worth knowing about up front.

### MediaPipe Iris is trained on the LEFT eye only
We run it on the right eye by mirroring the input crop horizontally (`mirrorX=true` in the `CameraInputRect`) and un-mirroring the output X coords (`x' = 1 - x`). One model file, two invocations per frame. See `iris_landmarker.cpp`.

### BlazeFace short-range emits exactly 896 anchors
The number is determined by the SsdAnchorsCalculator config in `blazeface_decoder.cpp`. If you ever see a different number, the model variant doesn't match the config — fall back to the dedicated config for that variant rather than tweaking constants.

### Skin tone updates are visible to effects 1-3 frames late
This is intentional. The async readback pattern avoids stalling the render thread. Don't try to make it synchronous; the latency is invisible to users because skin tone changes are slow.

### TFLite GPU delegate's "experimental flag" naming changes between versions
The exact flag enabling persistent GL textures has a name that varies. Check against the specific TFLite version you're linking. There's a TODO in `tflite_backend.cpp` flagging this.

### Concave contours produce overlapping triangles in the mask rasterizer
The lip outline is mildly concave at the cupid's bow. Our simple triangle-fan rasterizer produces overlap in concave regions. The smoothstep at the end of the shader clamps it back to 1.0, so the final mask is correct, but the mask is internally slightly "hot" in those regions. If we ever need true concave handling (Phase 6+), switch to ear-clipping triangulation.

### The `decodeBlazeFaceOutput` had a stub period
Until Phase 1's BlazeFace decoder fix, the function returned an empty array. If you see code that *seems* to expect detections but doesn't get any, check git blame — the bug may be old.

### Two faces in frame: BlazeFace doesn't guarantee stable detection order
Frame K may emit faces in [A, B], frame K+1 in [B, A]. We rely on `FaceTracker` (Hungarian IoU assignment) to produce stable IDs. If you add per-face state, key by `faceId`, never by array slot.

### Model file paths differ between platforms
Android: extracted from APK assets to `filesDir/models/*.tflite` on first launch (see `extractModelsIfNeeded` in `CommunityARPlugin.kt`).
iOS: directly in `Bundle.main.resourcePath/*.mlmodelc`.
The C++ side gets a `BackendConfig.modelDirectory` string; don't hardcode paths.

### The `phase0_session_phase2_updates.cpp` file is a delta document
Like the round-1 and round-2 Phase 1 fix files, it documents additions to `Phase0Session` rather than rewriting the original. Next consolidation pass merges them. Until then, treat it as authoritative for what's been added.

---

## Performance budget (target: 30 fps on Snapdragon 7-class Android)

```
Per frame (33 ms budget):
  Camera input + GL setup            ~1 ms
  PerceptionPipeline                 ~15-20 ms
    FaceMesh                         8 ms
    Iris (2 eyes)                    4 ms
    HairSegmenter                    5 ms
    PnP + post-processing            1 ms
  EffectGraph                        ~2-5 ms (grows with effect count)
    MaskedRecolor (per effect)       ~1 ms
  Flutter texture handoff            ~1 ms
  ─────────────────────────────────
  Total                              ~20-27 ms (3-13 ms headroom)
```

If a PR adds more than ~1 ms per frame on its claimed target hardware, justify it in the description. The headroom is for *future effects*, not for current cleverness.

---

## When to deviate from these conventions

The conventions are opinionated because consistency makes the codebase maintainable. But they're not religious. Deviate when:

- A genuine technical reason makes the convention wrong for your case (write the reason in a comment).
- The convention itself is showing its age (open a Discussions thread and let's update it).
- You're prototyping in a feature branch (just don't merge the deviation without discussion).

Don't deviate because the convention is inconvenient or you didn't read this file.

---

## Things to ask before writing code

If you're about to add a non-trivial feature, this short checklist saves both your time and review time:

1. **Does an existing effect engine cover this?** If yes, the answer is a factory function, not a new engine.
2. **What `PerceptionInputs` does it require?** Make sure the perception pipeline supports them; if not, the perception change is its own PR.
3. **Does it need to support multiple faces?** Almost certainly yes — design the data structures accordingly.
4. **Is there per-track state?** If yes, key by `faceId` and add it to the `retainOnly` GC pattern.
5. **Will it work on dark skin tones?** Verify via Oklab math, not RGB. Test on real faces.
6. **Performance budget?** Profile on a mid-range Android device, not just an iPhone Pro.
7. **What's the user-facing API?** Show me 3 lines of Dart that would use it.

---

## Status of this document

CLAUDE.md is itself versioned and evolves with the codebase. When you add a new invariant, update this file in the same PR. When you encounter a gotcha not listed here, add it. The next contributor will thank you.
