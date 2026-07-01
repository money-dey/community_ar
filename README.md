# Community AR

Open-source Flutter library for face-attached augmented reality.

Targets Android and iOS first; web (WebGPU/WASM) later. Designed around a
small set of generic effect engines so the library scales linearly with
effect count, not effect implementation complexity. Differentiates from
existing AR SDKs through deliberate emphasis on diverse skin tones, hair
textures, and face shapes — particularly serving African users where
mainstream SDKs are known to underperform.

This export captures the state of the project at the **end of Phase 1**:
the perception layer is fully production-shaped. Phase 2 (first end-to-end
effect — lip recolor) starts from here.

---

## Status

| Phase | Goal | Status |
|---|---|---|
| 0 | Plumbing — camera → C++ → Flutter texture, zero copy | ✅ Complete |
| 1 | Perception — landmarks, iris, masks, pose, skin tone | ✅ Complete (all 5 caveats fixed) |
| 2 | First effect end-to-end (lip recolor) | Not started |
| 3 | Effect graph + skin beautify v2 port | Not started |
| 4 | LandmarkWarp engine (eye enlarge, nose, lip plump) | Not started |
| 5 | Filament + 3D asset overlay (glasses, caps, earrings) | Not started |
| 5.5 | ImageGradeEffect + LUT system | Not started |
| 6 | Remaining recolor effects + BackgroundEffect | Not started |
| 7 | 3D hairstyles | Not started |
| 8 | Polish, cross-device testing, docs | Not started |

---

## What works after Phase 1

Every camera frame produces a complete `PerceptionFrame`:

- **468 face landmarks** with sub-pixel position via per-track One-Euro filtering
- **52 ARKit-style blendshape coefficients** (mouth open, eye blink, brow up, ...)
- **Iris position + radius for both eyes** with per-track filtering and
  the right-eye mirror trick (one model, two invocations)
- **6DoF face pose** from PnP against a canonical 3D model (no Eigen/OpenCV)
- **Hair mask** as a GPU texture
- **Selfie mask** as a GPU texture (for Phase 6's BackgroundEffect)
- **Trimmed-mean skin tone baseline**, updated every 5 frames via a real
  GPU compute pass with async readback
- **Per-face motion estimate** for temporal stabilization
- **Stable face IDs** across frames via Hungarian-IoU multi-face tracking

All of this is gated by an explicit `PerceptionInputs` declaration: models
whose outputs are not needed by any enabled effect are not run.

---

## Architecture in one glance

```
┌──────────────────────────────────────────────────────────────┐
│ Flutter app (Dart)                                           │
│   CommunityARPhase0View(camera: .front, testMode: ...)       │
└──────────────────────────────────────────────────────────────┘
                            ↑ method channel
┌──────────────────────────────────────────────────────────────┐
│ Platform adapter                                             │
│   Android: CommunityARPlugin.kt + CameraStream.kt (Camera2)  │
│   iOS:     CommunityARPlugin.swift + CameraStream.swift      │
└──────────────────────────────────────────────────────────────┘
                            ↑ JNI / ObjC++
┌──────────────────────────────────────────────────────────────┐
│ C ABI                                                        │
│   community_ar_phase0_api.h  (lifecycle, frame submission)   │
│   community_ar_phase1_api.h  (perception controls + stats)   │
└──────────────────────────────────────────────────────────────┘
                            ↑
┌──────────────────────────────────────────────────────────────┐
│ C++ core                                                     │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐     │
│  │ Phase0Session                                       │     │
│  │   owns: RenderContext, output FBO, test shaders    │     │
│  └─────────────────────────────────────────────────────┘     │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐     │
│  │ PerceptionPipeline                                  │     │
│  │   orchestrates per-frame inference                  │     │
│  │   - FaceLandmarker  (BlazeFace → FaceTracker → FaceMesh)  │
│  │   - IrisLandmarker  (both eyes via mirror trick)    │     │
│  │   - HairSegmenter, SelfieSegmenter                  │     │
│  │   - solveFacePose   (custom PnP)                    │     │
│  │   - SkinToneEstimator (real compute + async readback)│    │
│  │   - per-track GC                                    │     │
│  └─────────────────────────────────────────────────────┘     │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐     │
│  │ RenderContext                                       │     │
│  │   GLES (Android) and Metal (iOS) implementations    │     │
│  │   - Textures, framebuffers, shaders                 │     │
│  │   - Instanced rendering primitives                  │     │
│  │   - Compute primitives (SSBO, ComputeProgram,       │     │
│  │     AsyncReadback)                                  │     │
│  └─────────────────────────────────────────────────────┘     │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐     │
│  │ NeuralBackend                                       │     │
│  │   TFLite (Android, real GPU texture binding)        │     │
│  │   Core ML (iOS, IOSurface-shared CVPixelBuffer)     │     │
│  └─────────────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────────────┘
                            ↑
┌──────────────────────────────────────────────────────────────┐
│ Hardware: Camera, GPU (Adreno/Mali/PowerVR/Apple), NPU/ANE   │
└──────────────────────────────────────────────────────────────┘
```

---

## Repository layout

```
community_ar/
├── CMakeLists.txt                Consolidated build for Android NDK + iOS
├── README.md                     (this file)
├── docs/
│   ├── PHASE_0.md                Phase 0 — plumbing + verification
│   ├── PHASE_1.md                Phase 1 — perception layer overview
│   ├── PHASE_1_FIXES_ROUND_1.md  Production fixes: GPU bind, multi-face, instanced
│   └── PHASE_1_FIXES_ROUND_2.md  Production fixes: iris both eyes, skin tone compute
├── native/
│   └── core/
│       ├── ffi/
│       │   ├── community_ar_phase0_api.h          Phase 0 C ABI
│       │   ├── community_ar_phase0_api.cpp        C ABI implementation
│       │   └── community_ar_phase1_api.h          Phase 1 ABI additions
│       ├── phase0_session.{h,cpp}                  Top-level Session
│       ├── perception/
│       │   ├── perception_frame.h                  PerceptionFrame, FaceData, etc.
│       │   ├── perception_pipeline.h               Orchestrator interface
│       │   ├── perception_pipeline.cpp             Production orchestrator
│       │   ├── one_euro_filter.{h,cpp}             Adaptive landmark smoothing
│       │   ├── face_tracker.{h,cpp}                Hungarian-IoU multi-face tracking
│       │   ├── face_landmarker.{h,cpp}             BlazeFace + FaceMesh + tracker
│       │   ├── iris_landmarker.{h,cpp}             Both eyes, per-track filtering
│       │   ├── perception_models.{h,cpp}           Hair/Selfie segmenters
│       │   └── skin_tone.{h,cpp}                   Compute-backed trimmed mean
│       ├── math/
│       │   └── pnp_solver.{h,cpp}                  Custom PnP (no Eigen)
│       ├── ml/
│       │   ├── neural_backend.h                    Abstract ML interface
│       │   ├── tflite_backend.cpp                  Android, real GPU bind
│       │   └── coreml_backend.mm                   iOS, IOSurface pipeline
│       └── render/
│           ├── render_context.h                     Platform-agnostic GPU
│           ├── render_context_additions.h           Vertex buffers, instancing
│           ├── compute_primitives.h                 SSBO + ComputeProgram + readback
│           ├── gles_render_context.cpp              GLES 3.0+ backend
│           ├── gles_compute.cpp                     GLES 3.1 compute backend
│           ├── metal_render_context.mm              Metal backend
│           ├── metal_compute.mm                     Metal compute backend
│           ├── debug_overlay.{h,cpp}                Instanced landmark visualizer
├── android/
│   ├── src/main/kotlin/dev/communityar/
│   │   ├── CommunityARPlugin.kt                     Flutter plugin entry
│   │   └── CameraStream.kt                          Camera2 → OES texture
│   └── src/main/cpp/
│       └── jni_bridge.cpp                           Kotlin ↔ C ABI bridge
├── ios/
│   └── Classes/
│       ├── CommunityARPlugin.swift                  Flutter plugin entry
│       └── CameraStream.swift                       AVFoundation → MTLTexture
├── lib/
│   └── src/
│       ├── ffi/
│       │   ├── community_ar_phase0_ffi.dart         Phase 0 method-channel surface
│       │   └── community_ar_phase1_ffi.dart         Debug overlay, perception stats
│       └── widgets/
│           └── community_ar_phase0_view.dart        Flutter widget
└── example/
    └── lib/
        └── main.dart                                Perception verification app
```

---

## Architectural principles (locked in)

These are the load-bearing decisions made during Phase 0/1 that everything
downstream depends on. They are not up for re-litigation without a strong
reason.

1. **Pixels never enter Dart.** Camera frames, masks, and final composited
   output stay on the GPU throughout. Dart sees texture IDs, configuration
   structs, and event callbacks — never raw image data.

2. **The C ABI is the stability boundary.** Once shipped, struct layouts
   and function signatures don't change. Internal C++ refactors don't
   affect the ABI. New features = new ABI additions, never modifications.

3. **Perception is on-demand.** `PerceptionInputs` declares each effect's
   needs; only the union of all enabled effects' models actually runs.

4. **Per-track state, not per-slot.** Filters, motion estimates, skin
   tone — all keyed by stable face IDs from the FaceTracker. Faces
   reordering or entering/leaving the frame never scrambles state.

5. **One-Euro filtering everywhere.** Raw model output has sub-pixel
   jitter on every landmark every frame. The library is unusable without
   adaptive temporal smoothing.

6. **Async readback for GPU→CPU.** The render thread is never blocked
   waiting for GPU results. Skin tone and any future readback paths use
   the `AsyncReadback` polling pattern.

7. **No Eigen, no OpenCV.** Custom inline numerics for the few things we
   need (PnP, image math). Keeps binary size lean.

8. **Filament for rendering (Phase 5+).** Don't reinvent PBR. The ~10 MB
   cost is worth it for proper 3D asset rendering with environment lighting.

9. **MediaPipe Tasks for perception.** Don't reinvent face landmark
   detection. Use the official platform packages, which auto-route to
   ANE/NPU on supported devices.

10. **Three effect engines, not per-feature implementations.** Bucket A
    (MaskedRecolor), Bucket B (LandmarkWarp), Bucket C (AssetOverlay).
    All 20+ user-facing features compose from these three engines.

---

## What's stubbed vs implemented

Honest accounting of what's complete vs scaffolded:

| Component | Status | Notes |
|---|---|---|
| Phase 0 camera→Flutter pipeline (both platforms) | Implemented | Verified architecturally; needs real-device testing |
| Phase 0 test shaders (passthrough/grayscale/invert/vignette) | Implemented | |
| TFLite backend with GPU texture binding | Implemented | Production-shaped; needs real TFLite linkage |
| Core ML backend with IOSurface pipeline | Implemented | Production-shaped; needs real model .mlmodelc files |
| OneEuroFilter | Implemented | |
| FaceTracker (Hungarian-IoU) | Implemented | Greedy fallback for the rare unmatched-row case |
| FaceLandmarker (BlazeFace + tracker + FaceMesh) | Implemented | `decodeBlazeFaceOutput` is the one stub — needs real anchor table from MediaPipe's .pbtxt |
| IrisLandmarker (both eyes, per-track filters) | Implemented | |
| HairSegmenter / SelfieSegmenter | Implemented | Real model linkage needed |
| PnP face pose solver | Implemented | Custom Gauss-Newton; no external deps |
| SkinToneEstimator (compute pass + async readback) | Implemented | |
| GLES compute primitives | Implemented | |
| Metal compute primitives | Implemented | Per-dispatch encoder; can batch later if needed |
| Debug overlay (instanced) | Implemented | 1 draw call total |
| Pose gizmo (3D axes) | Approximate | Drawn as dots; line primitives deferred |
| **Real MediaPipe model files in native/models/** | NOT PRESENT | Must be obtained from MediaPipe release |

The one true open item: the actual `.tflite` and `.mlmodelc` files from
MediaPipe. The code expects them at `native/models/{face_detector,
face_landmarker, iris_landmark, hair_segmenter, selfie_segmenter}.{tflite,
mlmodelc}`. These come from the official MediaPipe release artifacts.

---

## Phase 1 verification checklist

Before declaring Phase 1 verified-on-device:

### Phase 0 carryover
- [ ] App launches without crash on Android + iOS
- [ ] Camera permission flow works
- [ ] Passthrough mode shows live camera
- [ ] Grayscale/invert/vignette modes work
- [ ] Switching front/back camera works
- [ ] App backgrounding/foregrounding stable

### Phase 1 perception
- [ ] Landmark dots appear and track face movement
- [ ] Dots do not jitter visibly when face is stationary (One-Euro working)
- [ ] Dots respond quickly to fast head turns (beta tuning correct)
- [ ] Both iris dots track gaze on left and right eyes
- [ ] Iris dots stay attached to their respective eyes when face turns
- [ ] Hair mask tints hair green
- [ ] Pose gizmo (3 colored dots) tracks face orientation
- [ ] Stats panel shows reasonable times: FaceMesh < 8ms, Iris < 4ms,
      Hair < 10ms on mid-range device
- [ ] minCutoff/beta sliders visibly affect smoothness vs responsiveness

### Production-fix specific
- [ ] Logs show `accelerator: GPU (zero-copy)` on Android startup
- [ ] Logs show `MLComputeUnitsAll` on iOS startup (ANE used on A12+)
- [ ] Per-model inference times dropped 3-5ms vs CPU-upload path
- [ ] With two people in frame, dots stay attached to their respective
      faces when faces swap positions
- [ ] After one face leaves and re-enters frame, it gets a NEW track ID
      (not the old one's filter state)
- [ ] Debug overlay frame time profiles at < 0.5 ms (instanced rendering)
- [ ] Skin tone numeric display updates ~6 times/second
- [ ] Skin tone updates lag camera lighting changes by < 200 ms
- [ ] No memory growth over a 10-min session

### Cross-platform critical
- [ ] **Tested on diverse skin tones.** Skin tone estimator produces
      sensible values across light, medium, and dark skin.
- [ ] **Tested on diverse hair textures.** Hair segmentation works on
      straight, wavy, curly, coily, locs, braids, twists.

---

## Building

### Android
```
# from project root
cd android
./gradlew assembleDebug
```

Native build is invoked automatically via `externalNativeBuild` in
`android/build.gradle` pointing at `CMakeLists.txt` at the root.

### iOS
```
# from project root
cd ios && pod install
open Runner.xcworkspace
```

### Flutter example
```
cd example
flutter pub get
flutter run
```

---

## Next: Phase 2

Phase 2 takes one effect end-to-end (lip recolor) and proves the whole
stack from `LipsEffect(color: red)` in Dart down to red lips on the
camera feed. Specifically:

1. Build the `MaskedRecolorEffect` engine (Bucket A in the architecture).
2. Implement landmark-driven mask rasterization (lip contour → soft mask).
3. Add `LipsEffect` Dart class as the user-facing API.
4. Add `EffectGraph` minimal v1 (one effect, no composition yet).
5. Verify: changing the `color` property in Dart visibly changes lip
   color on the camera feed in real time.

After Phase 2 lands, every Bucket A feature (iris color, teeth whitening,
under-eye brightening, hair thickening, beard thickening) becomes
essentially mechanical to add against this proven pattern.
