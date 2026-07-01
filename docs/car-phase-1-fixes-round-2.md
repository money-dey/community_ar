# Community AR Phase 1 — second-round fixes

Two remaining Phase 1 caveats closed:

| # | Caveat | Status |
|---|---|---|
| 4 | Iris model right-eye pass omitted | **Fixed** — both eyes + per-track filters |
| 5 | Skin tone GPU compute pass stubbed | **Fixed** — real dispatch + async readback |

## Fix 4 — Iris: both eyes + per-track temporal smoothing

### What got wired up
- **Right-eye pass.** MediaPipe Iris is trained on the left eye. We run the
  same model on the right eye by feeding a horizontally-mirrored crop, then
  unmirror the output X coordinates (`x' = 1 - x`). One model file ships,
  two invocations per frame.
- **Per-track filter banks.** Iris filters now live keyed by `faceId`, the
  same way face landmark filters do. Two faces no longer scramble each
  other's iris state, and a face that leaves and re-enters the frame gets
  fresh state (no jump from stale filters).
- **Per-component filtering.** Each eye has independent filters for the
  5 contour points, the derived center, and the radius. The radius uses
  a slightly slacker beta (`beta * 0.5`) because radius noise is mostly
  contour-detection wobble, and a stiffer filter reads naturally.

### Files
- `iris_landmarker.{h,cpp}` — replaces the previous half-implementation
- `perception_pipeline_iris_skin_updates.cpp` — passes `timestampSec`
  through to `IrisLandmarker::run()`, calls `retainOnly()` for GC

### Why mirroring works
The iris is bilaterally symmetric in appearance. The model learns "this
shape, in this orientation" → "iris contour at these points." Mirroring
the input preserves the shape and the relative orientation of features
(eyelid above iris, etc.), so the model's predictions stay valid; we just
need to unmirror the X coordinates back. The Y coordinates and radius
estimates are unchanged.

## Fix 5 — Skin tone: real compute dispatch + async readback

### New `RenderContext` primitives
The Phase 1 scaffold documented these as needed but never added them. The
second-round fix introduces:

- **`ShaderStorageBuffer`** — SSBO (GLES) / `MTLBuffer` (Metal), with
  explicit storage mode (`GpuOnly` / `Shared`).
- **`ComputeProgram`** — compiled compute shader. GLSL ES 3.1 on Android,
  MSL on iOS.
- **`AsyncReadback`** — handle representing an in-flight CPU readback,
  polled via `isReady()` until the GPU completes.

These three primitives are reusable for anything else we want to do with
compute later (e.g., custom mesh deformation, real-time mesh-based skin-
tone refinement).

### The complete data flow
```
Frame N:
  1. CPU computes 32 sample UVs from face landmarks → uvsBuffer (Shared SSBO)
  2. computeCtx->dispatchCompute(skinSampleProgram, 1, 1, 1)
     ⤷ GPU runs 32 invocations, each sampling cameraTex at one UV
       and writing RGBA → outBuffer (Shared SSBO)
  3. computeCtx->storageBufferBarrier()
  4. computeCtx->requestReadback(outBuffer, 512B)
     ⤷ on GLES: glFenceSync + glMapBufferRange
     ⤷ on Metal: cb.addCompletedHandler{ ready = true }

Frame N+1, N+2, ...:
  5. SkinToneEstimator::getCurrent() polls outstanding readbacks
  6. When isReady() is true: read RGBA samples, sort by luma, take middle
     60%, compute trimmed mean RGB and luma → SkinToneEstimate
  7. Estimate stored in the per-face cache; effect shaders consume it as
     a uniform.
```

### Worst-case latency
- 1 frame to issue the dispatch
- 1–2 frames for GPU completion
- Total: **~3 frames = 100 ms at 30 fps**

This is invisible because skin tone changes slowly (varies meaningfully
only with lighting changes, which themselves play out over seconds).

### Throttling and back-pressure
- One dispatch per **5 frames per face** (6 dispatches/second at 30 fps).
- Cap of **2 outstanding readbacks per face**. If a third would be issued
  before the oldest completes, the oldest is dropped — we never accumulate
  latency.

### Files
- `compute_primitives.h` — abstract interface
- `gles_compute.cpp` — OpenGL ES 3.1 implementation (SSBO + fence + map)
- `metal_compute.mm` — Metal implementation (MTLBuffer + command-buffer
  completion handler)
- `skin_tone.cpp` — now actually does what its header promises

### GLSL vs MSL
The compute shader source is duplicated as a temporary measure — GLSL ES
3.1 on Android, MSL on iOS. The Phase 5 SPIRV-Cross integration (planned
for the Filament work) will unify these so we ship one source per shader.

## Combined Phase 1 status

After the two rounds of fixes, the Phase 1 caveat list is fully closed:

| # | Caveat | Round |
|---|---|---|
| 1 | TFLite/Core ML GPU texture binding stubbed | Round 1 ✓ |
| 2 | Multi-face stubbed at 1 face | Round 1 ✓ |
| 3 | 468 draw calls for landmark dots | Round 1 ✓ |
| 4 | Iris right-eye omitted | Round 2 ✓ |
| 5 | Skin tone compute pass stubbed | Round 2 ✓ |

## Verification additions

Test cases that specifically exercise the round-2 fixes:

- [ ] Both iris dots track gaze movement smoothly on both eyes
- [ ] Iris dots remain attached to their respective eyes when face turns
- [ ] When a second face enters, the first face's iris state does not jump
- [ ] When a face leaves and re-enters, iris state starts fresh (no jump)
- [ ] Skin tone numeric display (debug overlay) updates ~6 times/second
- [ ] Skin tone updates lag camera movement by <200ms (visible on a
      quick light-change test)
- [ ] No frame-rate regression with skin tone enabled vs disabled
- [ ] Memory does not grow over a 10-min session — pending readbacks
      and per-track state are GC'd
