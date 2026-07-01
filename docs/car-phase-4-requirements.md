# Community AR Phase 4 — Upfront requirements

Decisions to lock before starting Phase 4 (LandmarkWarp engine).

The goal of this document is to lock the API shapes, file layout, and
architectural contracts that every Bucket B effect (`EyeEnlargeEffect`,
`NoseReshapeEffect`, `LipPlumpEffect`, `FaceSlimEffect`) will inherit —
the same pattern Phase 2 established for Bucket A (`MaskedRecolorEffect`)
and Phase 3 for `SkinSmoothEffect`.

**Read [`STRATEGY.md`](STRATEGY.md) before starting Phase 4
implementation.** It records a scoping question that was left open at
Phase 4 planning time: whether to ship all four warp effects (Position A)
or ship one and reinvest the saved time in the project's actual
differentiator (Position B). This document specifies the engine and
all four effects; the decision on how many of them to expose in the
initial Phase 4 release is captured in `STRATEGY.md`. The engine work
is identical either way.

**Read [`CARRIED_FORWARD.md`](CARRIED_FORWARD.md) before writing
implementation code.** Two entries interact directly with Phase 4:
- The whole-frame auto-tier benchmark note, which explains why the
  Phase 3 benchmark won't automatically account for Phase 4's warp cost
- The `FaceData.motion` field-name verification note, which needs to
  be resolved before writing the temporal smoothing code

---

## Goals

- A `LandmarkWarpEffect` engine — the third effect engine, alongside
  Phase 2's `MaskedRecolorEffect` and Phase 3's `SkinSmoothEffect`.
- Four user-facing Dart classes: `EyeEnlargeEffect`, `NoseReshapeEffect`,
  `LipPlumpEffect`, `FaceSlimEffect`.
- Composition into the existing `EffectGraph` at `passOrder=Warp` (300),
  sorted automatically after beauty (100) and recolor (200).
- Cross-skin-tone correctness — warps don't visibly degrade on faces
  with less-distinct landmark contrast.
- Multi-face support — up to 4 simultaneous warps, no cross-face
  interference.
- Performance: combined warp shader cost < 1.5 ms typical on
  Snapdragon 7-class, < 2.0 ms worst-case with all four warps active.

## Non-goals (deliberately deferred)

- **Generalized animation framework.** Per-effect localized easing only,
  matching Phase 3's specular ease. Keyframes, blendable transitions,
  and timeline coordination deferred to whenever a real use case
  emerges (probably Phase 5+ with 3D asset overlays).
- **Per-face independent warp parameters.** All detected faces get the
  same warp values (matching Phase 3's beauty behavior). Per-face
  customization deferred to Phase 6+.
- **Recolor-follows-warp re-pass.** Recolor effects run at
  `passOrder=Recolor` (200), before warps at 300. Lipstick gets
  stretched by lip plumping; that's the documented behavior. Re-running
  recolor after warp is out of scope.
- **Whole-frame auto-tier benchmark integration.** Phase 3's benchmark
  measures only beauty time. Phase 4 warps add to frame time without
  triggering tier adjustments. Documented in `CARRIED_FORWARD.md` as a
  Phase 5 task alongside GPU timer queries.
- **Pose-decoupled warps.** Warps use already-smoothed landmark
  positions and accept that expression changes affect displacement.
  Decoupling identity-shape warps from expression-driven landmark
  movement is out of scope.

---

## Architectural decisions to lock in

### Decision 1: Displacement field representation

**Pre-rasterize a displacement field to a small texture; main warp
shader samples from it.**

Per-frame pipeline:

1. **Displacement compute pass** rasterizes into a 256×256 RG16F
   texture. Each warp effect contributes triangles or quads with per-
   vertex displacement attributes; the rasterizer's linear interpolation
   gives smooth falloff without additional math.

2. **Temporal smoothing pass** blends the current displacement with the
   previous frame's, motion-gated (same shape as Phase 3's P6.5).

3. **Main warp pass** samples the displacement texture at each output
   pixel's UV, scales by the refined skin mask weight for natural
   feature-edge blending, and samples the input frame at the displaced
   coordinates.

**Why texture-based over shader-computed** (per-pixel iteration over
landmark positions):
- Separates landmark math from per-pixel warping — debuggable, and
  the displacement field can be visualized with a debug toggle
- Enables temporal smoothing as a separate concern
- Cost is fixed at small-texture rasterization plus one extra texture
  sample per output pixel, vs per-pixel iteration that scales with
  landmark count and produces visibly worse fragment shader stalls
- Matches the documented industry pattern for real-time face warping

**Texture format:** RG16F. Channels store displacement.x and
displacement.y in normalized [-1, 1] range. R16F alone is insufficient
(warps have direction); RGB16F wastes a channel.

**Texture resolution:** 256×256. The displacement field is inherently
smooth (low-frequency), so quantization isn't visible at this size
when bilinear-filtered up to 1080p. If real-device testing shows
banding at high warp strengths, bump to 512×512 (still ~1 MB, still
plenty of resolution).

### Decision 2: Warp falloff function

**Continuous radial falloff per landmark; contributions sum across
landmarks within an effect; clamped to refined skin mask at sample time.**

The displacement at any point in the displacement texture is:

```
displacement(p) = mask(p) × Σ_i [ strength × falloff(distance(p, anchor_i)) × direction_i(p) ]
```

Where:
- `mask(p)` is the refined face-skin mask from Phase 3's P1, so warps
  don't extend into hair or background even if the displacement field
  technically would push them there
- `strength` is the user's effect strength, per-frame-eased
- `falloff(d)` is `smoothstep(falloffEnd, falloffStart, d)` — drops
  smoothly to zero outside the effect's influence radius
- `direction_i(p)` is the per-anchor direction: radial outward for
  eye enlarge, perpendicular-to-contour for face slim and lip plump,
  axial for nose reshape

**Why smoothstep:** C¹-continuous fields, no visible discontinuities at
influence boundaries. Cheaper than cubic splines; visually equivalent
for this use case.

### Decision 3: Effect composition order within the Warp pass

**Single combined warp shader for all active warp effects.**

When the user has multiple warps enabled (e.g. eye enlarge + face slim),
the displacement compute pass accumulates contributions from all of
them into the same displacement texture *before* the main warp pass
runs.

Result: **one main warp pass per frame regardless of how many warps
are active**, not one pass per warp. Important for performance at the
frame budgets we care about.

Implementation: the four `LandmarkWarpEffect` instances share the
displacement texture via the mask pool. The first warp effect's
`prepare()` calls `ensureWarpResources()` (allocates or reuses the
displacement texture). Each `prepare()` contributes its displacement
via a rasterization call into the shared framebuffer. The `render()`
method of the last warp effect runs the main warp pass; the earlier
ones' `render()` calls are no-ops.

**"Last warp effect" detection**: the graph runs effects in `passOrder`
order, so among effects with `passOrder=Warp` the ordering is stable
declaration order. The last one is the one whose `render()` finds the
next chained effect isn't a `LandmarkWarpEffect`. Ugly but avoids
special-casing the graph for engine-level coordination.

**Alternative considered:** A separate `WarpCoordinator` object that
isn't itself an `Effect`. Rejected because it requires the graph to
special-case engines vs. effects, which perturbs the graph API for
one engine's benefit. The coordination-via-shared-state approach keeps
the graph unchanged.

### Decision 4: Multi-face displacement composition

**Per-face displacement contributions composed via maximum-influence,
not sum.**

Naive summation across faces produces "field tension" at pixels where
two faces' influence zones overlap — displacements pull in conflicting
directions, resulting in visibly weird distortion between the faces.

Instead: for each displacement-texture pixel, take the contribution
from the face with the strongest influence at that pixel. Implemented
via `glBlendEquation(GL_MAX)` when contributions are rasterized.

**Hard cap:** 4 simultaneous warps (matches Phase 1's face tracking
limit). Beyond 4 faces, warp only the 4 largest detected (by AABB,
same dominant-face selection Phase 3 uses).

### Decision 5: Animation strategy

**Per-effect localized easing only — no shared animation framework.**

Each warp effect has an internal `currentStrength_` field that
exponentially eases toward `cfg_.strength` at ~5 frames. The
displacement contributions use `currentStrength_`, not `cfg_.strength`.

Matches Phase 3's specular easing pattern. Implementation: ~10 lines
per effect.

Adding keyframe animation, blendable transitions, or coordinated
multi-effect animation requires real use cases. Defer until 3D asset
overlays (Phase 5+) bring requirements like "glasses fade in on a
timed entrance."

### Decision 6: Effect type IDs

Phase 2 reserved IDs 32-63 for Bucket B (LandmarkWarp). Confirmed:

| ID | Effect | Ships in |
|---|---|---|
| 32 | `EyeEnlargeEffect` | Phase 4 |
| 33 | `NoseReshapeEffect` | Phase 4 (or Phase 4.5 if Position B) |
| 34 | `LipPlumpEffect` | Phase 4 (or Phase 4.5 if Position B) |
| 35 | `FaceSlimEffect` | Phase 4 (or Phase 4.5 if Position B) |
| 36-63 | (reserved) | future Bucket B |

Per invariant 2 in `CLAUDE.md`, reserved IDs stay reserved even if
their effects are deferred. Types 33-35 are not reclaimable.

### Decision 7: Edge sampling behavior

**`CLAMP_TO_EDGE` on the input texture.** When the warp samples a UV
outside [0, 1], return the edge pixel. Least-noticeable artifact at
image edges compared to transparent-black (visible black streaks) or
wrap (visible seams during strong warps).

For face warps confined to the face region — which is typically well
inside the frame — this rarely matters in practice. Listed as a known
minor edge artifact in the verification checklist.

### Decision 8: Landmark source

**Always use `PerceptionFrame.faces[i].landmarks.points`** — the
already-One-Euro-smoothed positions from Phase 1. Never re-derive from
raw network output.

The One-Euro filter produces stable landmark positions even when raw
network output is jittery. Skipping this filter would re-introduce
jitter that the warp would then visibly amplify.

### Decision 9: Per-effect landmark selections

The specific landmark indices each warp effect uses. Chosen deliberately
to avoid MediaPipe FaceMesh's known weak spots (jaw corners at indices
58/288, raw bridge points, cheek midpoints).

**`EyeEnlargeEffect`:**
- Anchor centers: `iris.leftCenter`, `iris.rightCenter` from
  `PerceptionFrame.iris` (more stable than landmark approximations
  of eye centers, per Phase 1's iris detector)
- Influence radius: ~1.5 × iris radius
- Direction: radial outward from each eye center
- Default strength: 0.3; max recommended: 0.6

**`NoseReshapeEffect`:**
- Bridge landmarks: 168, 8
- Wing landmarks: 219, 459
- Tip landmark: 1
- Two sub-controls: `lateralPinch` [0, 1] (squeezes wings inward) and
  `tipLift` [0, 1] (raises tip vertically)
- Defaults: `lateralPinch` 0.2, `tipLift` 0.0

**`LipPlumpEffect`:**
- Outer lip contour: same indices Phase 2's `LipsEffect` uses (61, 185,
  40, ..., 146 for outer upper + lower)
- Direction: perpendicular to contour, outward
- Default strength: 0.25; max recommended: 0.5
- Inner contour points unaffected — only the outer lip edge moves

**`FaceSlimEffect`:**
- Jawline contour: 172, 136, 150, 149, 176, 148, 152, 377, 400, 378,
  379, 365, 397 (well-tracked jaw outline, avoiding the jittery jaw
  corners 58 and 288)
- Direction: perpendicular to jawline, inward toward face center
- Default strength: 0.2; max recommended: 0.4
- Falloff is asymmetric — strong at the jaw, fading by the temple

### Decision 10: Effect graph integration

**Each warp effect declares:**
- `typeId()`: from Decision 6
- `passOrder()`: `EffectPass::Warp` (300)
- `perceptionInputs()`: `needsFaceLandmarks=true`, `needsFaceSkinMask=true`;
  `needsIrisLandmarks=true` for `EyeEnlargeEffect` specifically
- `maskRequirements()`: consumes `"masks.refinedFaceSkin"` (from
  Phase 3's `SkinSmoothEffect` P1) OR a fallback computed inline

**Fallback path matters:** users can enable warps without enabling
beauty. In that case no `masks.refinedFaceSkin` exists. The engine
falls back to raw `masks.faceSkin` from the segmenter; if that's also
missing (fallback backend has no face-skin channel), it rasterizes a
coarse face oval mask from landmark points.

```cpp
auto mask = maskPool.get("masks.refinedFaceSkin");
if (!mask) mask = maskPool.get(MaskResourcePool::kFaceSkin);
if (!mask) mask = rasterizeFaceOvalMask(frame.faces[0].landmarks);
```

---

## File-by-file plan

### New files

```
native/core/effects/
├── landmark_warp_effect.{h,cpp}                Engine + coordinator
├── warp_shader_displacement_compute.h          GLSL: writes displacement texture
├── warp_shader_displacement_temporal.h         GLSL: temporal smoothing
├── warp_shader_main.h                          GLSL: full-res warp
├── eye_enlarge_effect.cpp                      Factory + landmark selection
├── nose_reshape_effect.cpp                     Factory + landmark selection
├── lip_plump_effect.cpp                        Factory + landmark selection
└── face_slim_effect.cpp                        Factory + landmark selection

native/core/ffi/
├── community_ar_phase4_api.h                   CARxxxEffectConfig structs
└── community_ar_phase4_api.cpp                 Factory registration

lib/src/effects/
├── eye_enlarge_effect.dart                     User-facing class + config
├── nose_reshape_effect.dart
├── lip_plump_effect.dart
└── face_slim_effect.dart

docs/car-phase-4.md                                 What shipped (written at end of phase)
```

If Position B is chosen (per `STRATEGY.md`), the three non-eye factory
`.cpp` files and their Dart classes are deferred — the engine gets
built to support them, but they don't ship in the initial Phase 4
release.

### Modified files

- `native/core/effects/effect_types.h` — add IDs 32-35
- `native/core/ffi/community_ar_phase2_api.cpp` — register new factories
  in the effect type dispatcher
- `lib/community_ar.dart` — export new effect classes + configs
- `CMakeLists.txt` — add new C++ sources
- `example/lib/main.dart` — tabbed bottom panel (Beauty | Color | Shape)
- `FEATURES.md` — move Phase 4 items from Planned to Shipped
- `README.md` — status table + quickstart snippet

---

## API shapes

### Dart public API (eye enlarge example — pattern applies to all four)

```dart
class EyeEnlargeEffect extends Effect {
  /// Enlargement strength. Range: [0, 1]. Default: 0.3.
  /// Values above 0.6 produce visibly uncanny results.
  final double strength;

  const EyeEnlargeEffect({this.strength = 0.3}) {
    if (strength < 0 || strength > 1) {
      throw ArgumentError.value(strength, 'strength', 'must be in [0, 1]');
    }
  }

  @override
  int get typeId => 32;  // CAR_EFFECT_EYE_ENLARGE

  @override
  Uint8List serialize() {
    final bytes = ByteData(12);
    bytes.setUint32(0, 1, Endian.host);
    bytes.setFloat32(4, strength, Endian.host);
    bytes.setUint32(8, 0, Endian.host);  // reserved
    return bytes.buffer.asUint8List();
  }
}
```

Usage — automatic ordering:

```dart
CommunityARView(
  camera: CameraLens.front,
  effects: EffectGraph(effects: [
    SkinSmoothEffect(config: BeautyPresets.natural),
    LipsEffect(color: Colors.red),
    EyeEnlargeEffect(strength: 0.3),
    FaceSlimEffect(strength: 0.25),
  ]),
)
```

Graph runs: beauty (100) → lipstick (200) → warps (300).

`NoseReshapeEffect` is the only variant with two knobs:

```dart
class NoseReshapeEffect extends Effect {
  final double lateralPinch;  // [0, 1]
  final double tipLift;       // [0, 1]

  const NoseReshapeEffect({
    this.lateralPinch = 0.2,
    this.tipLift = 0.0,
  });
  // ... validation, typeId, serialize follow the same pattern
}
```

### C ABI surface

```c
// community_ar_phase4_api.h
#define CAR_EFFECT_EYE_ENLARGE      32
#define CAR_EFFECT_NOSE_RESHAPE     33
#define CAR_EFFECT_LIP_PLUMP        34
#define CAR_EFFECT_FACE_SLIM        35

typedef struct {
    uint32_t version;    // = 1
    float    strength;   // [0, 1]
    uint32_t reserved;
} CAREyeEnlargeEffectConfig;

typedef struct {
    uint32_t version;
    float    lateralPinch;
    float    tipLift;
    uint32_t reserved;
} CARNoseReshapeEffectConfig;

typedef struct {
    uint32_t version;
    float    strength;
    uint32_t reserved;
} CARLipPlumpEffectConfig;

typedef struct {
    uint32_t version;
    float    strength;
    uint32_t reserved;
} CARFaceSlimEffectConfig;
```

No new C ABI *functions* — effects install via existing `car_p2_graph_set`
with the new type IDs.

### C++ engine sketch

```cpp
// landmark_warp_effect.h
class LandmarkWarpEffect : public Effect {
public:
    struct Config {
        uint32_t typeId;                       // CAR_EFFECT_EYE_ENLARGE etc.

        std::vector<int> anchorLandmarkIndices;
        enum class DirectionMode {
            RadialOutward,           // EyeEnlarge
            PerpendicularToContour,  // LipPlump, FaceSlim
            Axial,                   // NoseReshape (tip lift)
            CompositeNose,           // NoseReshape (lateral + axial)
        } directionMode;

        // User-provided strengths (eased per-frame internally)
        float strength = 0.0f;
        float secondaryStrength = 0.0f;  // tipLift for NoseReshape

        // Falloff (in normalized image coordinates)
        float falloffStart = 0.05f;
        float falloffEnd   = 0.20f;

        // Direction inversion for the perpendicular-to-contour case
        bool inwardWarp = false;
    };

    explicit LandmarkWarpEffect(Config cfg);

    uint32_t typeId() const override { return cfg_.typeId; }
    EffectPass passOrder() const override { return EffectPass::Warp; }
    PerceptionInputs perceptionInputs() const override;
    MaskRequirements maskRequirements() const override;

    void prepare(const PerceptionFrame&, MaskResourcePool&,
                 RenderContext*) override;
    void render(const TextureHandle&, Framebuffer*,
                const MaskResourcePool&, RenderContext*) override;

private:
    void easeStrengths();
    void contributeDisplacement(RenderContext*);
    bool isLastWarpInGraph(const MaskResourcePool&) const;

    Config cfg_;
    float currentStrength_ = 0.0f;
    float currentSecondaryStrength_ = 0.0f;
    // GPU resources allocated lazily in prepare()
};
```

The four factory `.cpp` files each fill in the `Config` for one effect
type — same shape as Phase 2's `lips_effect.cpp` factory. About 30-50
lines each.

---

## Shader design

### `warp_shader_displacement_compute`

Rasterizes per-pixel 2D displacement vectors into the 256×256 RG16F
texture. Vertex shader receives triangle fans with per-vertex
displacement and influence weight attributes; fragment shader writes
interpolated displacement scaled by influence.

**Blending:** `GL_MAX` blend function so multi-face contributions take
maximum-influence per Decision 4. Contributions from within a single
face still sum naturally through rasterization (they don't overlap
because falloff zones are disjoint).

### `warp_shader_displacement_temporal`

Motion-gated history blend applied to the RG displacement channels,
identical in shape to Phase 3's P6.5 (which does the same operation on
RGB color).

Motion input is `frameState_.motionMagnitude` from the dominant face —
**note the field-name verification item in `CARRIED_FORWARD.md`.**

### `warp_shader_main`

```glsl
#version 300 es
precision mediump float;

uniform sampler2D uInputFrame;
uniform sampler2D uDisplacement;   // RG16F, 256×256
uniform sampler2D uSkinMask;        // R8, full-res

in  vec2 vUv;
out vec4 fragColor;

void main() {
    vec2 disp = texture(uDisplacement, vUv).rg;
    float maskWeight = texture(uSkinMask, vUv).r;

    // Early-out for negligible displacement (>95% of pixels)
    if (length(disp) < 0.001) {
        fragColor = texture(uInputFrame, vUv);
        return;
    }

    // Attenuate by skin mask so feature edges blend
    vec2 sampleUv = vUv + disp * maskWeight;
    sampleUv = clamp(sampleUv, vec2(0.001), vec2(0.999));

    fragColor = texture(uInputFrame, sampleUv);
}
```

The early-out is what keeps this pass under 0.7 ms — most output pixels
have zero displacement and skip the second texture sample.

---

## Performance budget

Total frame budget at 30 fps: 33 ms.
Phase 3 leaves ~23 ms after perception.
Phase 4 target: ≤ 1.5 ms typical, ≤ 2.0 ms worst-case (4 active warps).

Breakdown:
- Displacement compute pass: ~0.4 ms (256×256, light triangle count)
- Temporal smoothing pass: ~0.2 ms (256×256, single sample per pixel)
- Main warp pass: ~0.7 ms (full-res, early-out for ~95% of pixels)
- Overhead (FBO binds, shader switches): ~0.2 ms

With all four warps active, the displacement compute grows by
contribution work but the rest stays constant. Worst case ~2.0 ms.

---

## Verification checklist

### Functional
- [ ] `EyeEnlargeEffect(strength: 0.3)` produces visibly larger eyes
- [ ] `NoseReshapeEffect(lateralPinch: 0.3)` narrows the nose
- [ ] `LipPlumpEffect(strength: 0.25)` makes lips fuller
- [ ] `FaceSlimEffect(strength: 0.2)` narrows the jawline
- [ ] All four combined: composite warps look natural, not stacked-weird
- [ ] `strength = 0` produces no visible effect (degenerate case)
- [ ] Sliding strength produces smooth transitions (~5-frame ease)
- [ ] Graph ordering automatic: warps run after beauty + recolor
- [ ] Two faces in frame: both get warped, no visible cross-face
      interference (verify MAX blending correctness)

### Quality (cross-skin-tone — the differentiator)
- [ ] Warps stay locked to features during head rotation (±30°)
- [ ] No visible warp jitter on stationary faces, including on darker
      skin where underlying landmarks may be noisier
- [ ] Background pixels don't visibly shift when face moves
- [ ] Hair adjacent to face doesn't warp with the face (verify the
      skin mask confinement)
- [ ] Lipstick on plumped lips looks natural (some stretching is
      expected; shouldn't look broken)
- [ ] At default strengths, results don't read as "filtered" to a
      casual observer — enhancement, not transformation
- [ ] Cross-skin-tone verification: same strength values produce
      visually consistent results on light, medium, and dark skin

### Performance
- [ ] Single warp effect: < 1.0 ms total
- [ ] All four warps active: < 2.0 ms total
- [ ] No frame rate drop vs Phase 3 baseline at default warp strengths
- [ ] Shader early-out working: outside-skin pixels stay fast

### Architecture
- [ ] Single combined warp shader handles all active warp effects
      (verify only one main-warp pass runs per frame, not one per
      effect)
- [ ] Multi-face composition uses MAX blending (verify with two faces
      that displacements don't sum-interfere)
- [ ] Warps work without `SkinSmoothEffect` in the graph — fallback
      path to raw `masks.faceSkin`, then coarse landmark mask
- [ ] Adding/removing warp effects doesn't leak GPU resources
- [ ] All four `CARxxxEffectConfig` structs round-trip Dart → C ABI
      with byte-exact field offsets (ABI compatibility tests, per
      `TESTING.md`)

---

## Open questions to resolve before coding

1. **`NoseReshapeEffect`: one strength knob or two?** Current spec
   is two (`lateralPinch` + `tipLift`). Most users only adjust one at
   a time, and a single knob is easier to expose in UI.
   **Recommendation:** keep separate. Power users need both; the UI
   can show one slider that adjusts both proportionally if desired.

2. **Expose `falloffStart` / `falloffEnd` to users?** They affect how
   far each warp's influence reaches. **Recommendation:** hide for
   now. Add later if demand emerges.

3. **Clear vs accumulate displacement between frames?** Cleared.
   Accumulation would create trail artifacts during head motion.

4. **`EyeEnlargeEffect` fallback when iris isn't detected?** Use the
   eye-region landmark centroid as substitute for
   `iris.leftCenter`/`rightCenter`. Less stable than iris, but better
   than skipping the warp entirely.

5. **How to communicate the auto-tier limitation to users?** When
   warps are enabled alongside beauty, frame time may drift over the
   auto-tier threshold without triggering a tier drop. **Recommendation:**
   documentation note in `BeautyQuality` docs pointing users on
   borderline devices toward `BeautyQuality.medium` explicitly. The
   proper fix is in `CARRIED_FORWARD.md` for Phase 5.

6. **Position A or Position B?** Per `STRATEGY.md`, the scoping
   decision for Phase 4 is left open. Made when implementation begins.

---

## What Phase 4 unlocks

After Phase 4 ships, the warp engine becomes the third tool in the
project's effect-composition vocabulary. Future warp effects (forehead
smoothing, chin reshape, neck slimming) become factory-function-only
additions — same shape as Phase 6 uses for the remaining
MaskedRecolor effects.

Phase 5 (Filament + 3D asset overlays) begins after Phase 4 ships.
Filament brings PBR rendering, glTF asset loading, and — importantly —
the GPU timer queries that Phase 3 and Phase 4 both deferred (see
`CARRIED_FORWARD.md`).

The architectural surface remains stable: three engines, six pass
categories, one mask pool, one effect graph. Everything Phase 5+ adds
plugs into this surface; nothing reaches in to modify it.
