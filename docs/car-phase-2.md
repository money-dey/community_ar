# Community AR — Phase 2: First effect (lip recolor)

The first effect runs end-to-end. The user picks a lipstick color in Dart;
their lips change color on the live camera feed in real time.

This phase is where the project transitions from "infrastructure" to "doing
the thing." Everything Phases 0 and 1 built is now load-bearing: camera
pipeline (Phase 0), perception (Phase 1), effect engines (Phase 2).

## What's new vs Phase 1

| Component | Purpose |
|---|---|
| `Effect` (C++ abstract) | Base contract every effect implements |
| `EffectGraph` (C++) | Linear chain of effects with ping-pong FBOs |
| `MaskRasterizer` | Landmark contour → soft-edged mask texture |
| `MaskedRecolorEffect` | Bucket A engine — drives all 8 future recolor effects |
| `LipsEffect` (C++ factory) | Pre-fills MaskedRecolorEffect with lip contour indices |
| `LipsEffect` (Dart) | User-facing API with `color`, `opacity`, etc. |
| `EffectGraph` (Dart, immutable) | Value-typed declarative effect list |
| `CommunityARView` widget | Replaces `CommunityARPhase0View` for production use |
| Phase 2 C ABI | `car_p2_graph_set` / `car_p2_graph_clear` for atomic graph swaps |

## File map

```
native/core/
├── effects/
│   ├── effect_base.h               Abstract Effect interface
│   ├── effect_types.h              Stable numeric type IDs
│   ├── effect_graph.{h,cpp}        Linear chain + ping-pong FBOs
│   ├── mask_rasterizer.{h,cpp}     Contour → soft mask, GPU triangle fan
│   ├── masked_recolor_effect.{h,cpp}  Bucket A engine (Oklab recolor)
│   └── lips_effect.cpp             LipsEffect factory function
├── ffi/
│   ├── community_ar_phase2_api.h   ABI additions (config struct + graph ops)
│   └── community_ar_phase2_api.cpp ABI implementation
└── phase0_session_phase2_updates.cpp  Session integration

lib/
├── community_ar.dart               Public library export
└── src/
    ├── effects/
    │   ├── effect.dart             Effect base class
    │   ├── effect_graph.dart       Immutable value-typed graph
    │   └── lips_effect.dart        User-facing LipsEffect class
    ├── ffi/
    │   └── community_ar_phase2_ffi.dart  Method channel surface
    └── widgets/
        └── community_ar_view.dart  Production-facing widget

example/lib/main.dart               Lip color picker demo app
```

## Architectural decisions locked in (matching car-phase-2-requirements.md)

1. **GPU triangle rasterization with per-vertex alpha falloff** for mask
   production. Same pattern reused by every future Bucket A effect.
2. **Hand-rolled versioned binary structs** for effect configs across the
   C ABI. Not FlatBuffers; migration path documented.
3. **Oklab color space** for recolor math. Perceptually uniform, makes
   lipstick read the same on dark and light skin.
4. **Linear effect chain with ping-pong FBOs.** Atomic graph replacement
   on update. Dependency resolution and resource sharing deferred to Phase 3.
5. **Single mask texture for all faces.** Per-face customization deferred
   to Phase 6.
6. **Effect type IDs reserved by bucket.** 1-31 = Bucket A, 32-63 = Bucket B,
   64-127 = Bucket C, etc.
7. **MediaPipe FaceMesh outer-lip contour** for the mask, with the inner-lip
   contour subtracted when `jawOpen` blendshape exceeds 0.05 (so teeth/tongue
   don't get painted when mouth is open).
8. **Each effect declares its own perception requirements.** The
   EffectGraph computes the union and pushes to PerceptionPipeline.

## The lipstick experience

After running the example app:

- Tap one of 8 palette swatches → lip color changes within one frame
- Drag the **opacity** slider from 0 → 1 → lips fade from natural to fully
  painted
- Drag the **softness** slider → polygonal edges of the mask become invisible
  at higher values
- Drag the **preserve brightness** slider:
  - 1.0 = natural lipstick (keeps the lighting on your real lips)
  - 0.0 = flat painted look (lips become solid target color, no shading)
  - Values between = matte → gloss continuum
- Open mouth → teeth and tongue stay their natural color (inner-contour
  subtraction working)
- Move head fast → lipstick stays attached to lips (Phase 1 One-Euro filter
  working through the perception layer)

## Performance

At 30 fps on Snapdragon 7-class Android, Phase 2 effect overhead:

| Pass | Cost |
|---|---|
| Mask rasterization (1 face, 20 vertices) | ~0.3 ms |
| Oklab recolor shader (full screen) | ~0.5 ms |
| FBO bind + clear overhead | ~0.1 ms |
| **Total per frame** | **~0.9 ms** |

Combined with Phase 1 perception (~15-20 ms on the same hardware), the
full pipeline lands at ~20 ms per frame, leaving ~13 ms of headroom for
adding more effects in Phase 3+.

## What's deliberately deferred

Nothing in this list is "missing" — it was scoped out of Phase 2 intentionally
to keep the surface area minimal:

- **Effect composition.** Phase 2 renders the graph correctly with N effects,
  but the dependency-aware ordering, mask sharing, and parallel-execution
  optimizations land in Phase 3.
- **Per-effect config updates.** Currently the whole graph rebuilds when
  any property changes. Fine-grained `car_p2_effect_update_config` API
  will be added when graph size makes this an issue.
- **Animation.** Static configs only in Phase 2. Phase 4 introduces
  animation infrastructure.
- **Per-face customization.** Same lip color on every detected face.
  Phase 6 introduces per-face parameters.
- **UV-mapped artwork masks.** Phase 2 supports landmark-contour masks
  only. Detailed makeup (multi-color eyeshadow, complex patterns) needs
  UV-mapped textures, deferred until an effect needs them.

## Verification checklist

### Functional
- [ ] `LipsEffect(color: red)` produces visibly red lips on the camera feed
- [ ] Tapping a different palette swatch updates within one frame
- [ ] Lipstick stays attached to lips during head movement (perception working)
- [ ] Mouth opens → teeth/tongue are NOT painted (inner contour subtraction)
- [ ] Mouth closes → lipstick covers full lip area
- [ ] `opacity = 0` → no visible effect (degenerate case works)
- [ ] `opacity = 1, luminancePreserve = 0` → fully painted, flat-color look
- [ ] `opacity = 0.85, luminancePreserve = 1` → natural lipstick

### Quality (THE differentiator)
- [ ] Mask edges are soft, not polygonal at default softness
- [ ] **Works on dark, medium, and light skin tones with consistent visual
      quality** — this is the Oklab decision paying off; verify before
      shipping
- [ ] No color leakage onto chin / cheek / inside-of-mouth at default softness
- [ ] No temporal flicker on the mask edges when face is stationary

### Performance
- [ ] Total per-frame effect time < 2 ms (mask + shader)
- [ ] No frame rate drop vs Phase 1 baseline
- [ ] No visible latency added to head tracking

### Architecture (proves the engine is reusable)
- [ ] `EffectGraph` with one effect runs cleanly; replacing graph atomically works
- [ ] `LipsEffect.serialize()` → `CARLipsEffectConfig` → C++ round-trip
      preserves all six fields
- [ ] `MaskedRecolorEffect` takes config — no LipsEffect-specific code
      inside the engine (verify by reading masked_recolor_effect.cpp)
- [ ] Two faces in frame → both get lipstick
- [ ] Zero faces in frame → no crash, just camera passthrough

## What Phase 2 unlocks

Each of the remaining Bucket A effects (IrisEffect, TeethEffect, BrowsEffect,
UnderEyeEffect, HairThickenEffect, BeardThickenEffect) is now mechanically
one file each — just like `lips_effect.cpp` but with different contour
indices and possibly tweaked blend math:

```cpp
// iris_effect.cpp (Phase 6 preview)
std::unique_ptr<Effect> makeIrisEffect(const void* cfg, size_t size) {
    MaskedRecolorEffect::Config mc;
    mc.typeId = CAR_EFFECT_IRIS;
    mc.outerContourIndices = kIrisOuterContour;  // from PerceptionFrame.iris
    // ... 5 more lines, identical pattern ...
    return std::make_unique<MaskedRecolorEffect>(std::move(mc));
}
```

This is exactly why Phase 2's investment in the engine abstraction matters.
Phase 6 will add 6 effects in ~6 hours of work, not 6 weeks.
