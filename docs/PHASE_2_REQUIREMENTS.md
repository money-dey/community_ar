# Phase 2 — Upfront requirements

Decisions to lock before starting Phase 2 (first effect end-to-end: lip recolor).

The goal of this doc is to lock the API shapes, file layout, and architectural
contracts that every subsequent Bucket A effect (iris, teeth, brows, under-eye,
hair-thicken, beard-thicken, skin) will inherit. Phase 2 ships one effect
visibly; Phase 6 then adds the others as mechanical instances of the same
machinery.

---

## Goals

- A `LipsEffect(color: ...)` Dart class that changes lip color on the live
  camera feed in real time.
- A reusable `MaskedRecolorEffect` engine (Bucket A) that all 8 future
  recolor effects will be instances of.
- A minimal `EffectGraph` v1 that supports one effect today and grows to
  composition in Phase 3 without breaking changes.
- A lip mask rasterizer that produces soft-edged masks from FaceMesh
  landmarks — the pattern reused by every other contour-based effect.
- A serialization contract for effect configs across the C ABI that's
  stable enough to ship.

## Non-goals (deliberately deferred)

- Effect composition / multiple effects running together — that's Phase 3.
- Animation infrastructure (configs animating over time) — Phase 4.
- Effect-to-effect resource sharing (effects sharing a computed mask) — Phase 3.
- Per-face individual effect parameters (every face uses the same lip color
  in Phase 2; per-face customization comes in Phase 6+).
- UV-mapped mask textures for detailed artwork (e.g. multi-color eyeshadow) —
  deferred until we have an effect that actually needs it.
- ImageGradeEffect (LUT-based color grading) — Phase 5.5.

---

## Architectural decisions to lock in

### Decision 1: Mask production strategy

**Use GPU triangle rasterization with per-vertex alpha falloff. Defer SDF and
UV-mapped textures to later phases.**

For lip recolor we triangulate the lip contour into a triangle fan, where:
- Interior vertices have `alpha = 1.0` (full effect)
- Boundary vertices have `alpha = 0.0` (no effect)
- The fragment shader interpolates alpha across the triangle, giving soft
  edges naturally

This is the same pattern the Phase 1 instanced-dots overlay uses. Same cost
(~0.3ms for the lip contour), same approach for every future Bucket A effect.
When we eventually need detailed artwork (multi-color makeup), we'll add a
second `MaskSource` variant that uses UV-mapped textures — but the interface
that consumes the mask doesn't change.

**Why not SDF?** SDF gives the prettiest edges but requires a second pass to
compute the distance field, and the visual quality difference for ~10px soft
edges is below perceptual threshold. Save it for cases where it pays.

### Decision 2: Effect config serialization

**Hand-rolled binary structs with explicit version fields. No FlatBuffers in
Phase 2. Path to FlatBuffers documented for when we actually need schema
evolution.**

Each effect type defines a C struct with a `uint32_t version` as the first
field. Dart serializes via `ByteData`, C++ casts the pointer. New fields can
be appended (forward-compatible if we always check version before reading
new fields).

```c
// In community_ar_phase2_api.h
typedef struct {
    uint32_t version;        // = 1
    float    colorR, colorG, colorB;  // sRGB, [0, 1]
    float    opacity;        // [0, 1], overall effect strength
    float    edgeSoftness;   // [0, 1], how much to feather the mask edge
    float    luminancePreserve;  // [0, 1], how much original brightness to keep
} CARLipsEffectConfig;
```

The C ABI signature for adding an effect:
```c
CAR_EXPORT CARStatus car_p2_graph_add_effect(
    CARSession*    session,
    uint32_t       effect_type_id,
    const void*    config_bytes,
    size_t         config_size,
    uint32_t*      out_effect_handle  // opaque, for future addressing
);
```

**Why hand-rolled vs FlatBuffers?** One effect, six fields. FlatBuffers
adds ~30KB of binary and a code-generation step. When we have 15+ effect
types with frequent schema changes, that ratio flips. Document the migration
path in the doc, don't pre-build the migration.

### Decision 3: Color space for recolor math

**Oklab.** Consistent with beauty v2's perceptual color space; matters
disproportionately for diverse skin tones.

The shader path:
1. Sample input pixel → sRGB
2. Convert sRGB → linear → Oklab `(L, a, b)`
3. Compute target color in Oklab: `(L_target, a_target, b_target)`
4. Blend: `L' = mix(L_input, L_target, 1 - luminancePreserve)`,
   `a' = mix(a_input, a_target, opacity * maskAlpha)`,
   `b' = mix(b_input, b_target, opacity * maskAlpha)`
5. Oklab → linear → sRGB → output

Setting `luminancePreserve = 1.0` keeps the original lip's lighting and
just changes the chroma — which is what you want for natural-looking
lipstick. Setting it lower gives more painted/opaque looks.

**Why Oklab specifically?** The `a, b` axes are perceptually uniform, so the
same numeric blend produces the same visual change on dark and light skin.
This is the failure mode you see in apps that use naive RGB blending:
lipstick on darker skin reads as gray or muddy because the math is wrong.

### Decision 4: EffectGraph minimal v1

**Linear list of effects, run in declared order. Each effect reads the
previous effect's output and writes to a new framebuffer. Last effect's
output is the session's display texture.**

```
[Camera]
   ↓
[Effect 0 input FBO]  ──→  Effect 0 (LipsEffect)
   ↓
[Effect 0 output FBO == Effect 1 input FBO]  ──→  Effect 1 (future)
   ↓
[Display texture]  ──→  Flutter
```

In Phase 2 there's one effect, so input is the camera output and output is
the display texture directly.

**What we explicitly defer to Phase 3:**
- Dependency resolution (figuring out execution order from data flow)
- Parallel effect execution
- Mask-resource sharing between effects
- Effect insertion/removal without rebuilding the graph

**Graph mutability:** the graph is **immutable** in Phase 2. Changing one
effect's color creates a new graph and pushes the full new graph to native.
For 1-3 effects this is free. When the graph has 10+ effects and we want
fine-grained updates, we'll add a `car_p2_effect_update_config()` ABI call
keyed by the `effect_handle` from `add_effect`. For now: replace-the-whole-graph
is simpler and we don't have a perf problem to solve.

### Decision 5: Mask scope

**One mask texture for all faces, additive.** Every face's lip contour
contributes to the same mask. Same effect parameters apply to all faces.

In Phase 6 when per-face customization matters, we'll extend to one mask
per face. Until then, simpler is correct.

### Decision 6: Effect type IDs

**Reserved IDs (stability-bound forever):**

| ID | Effect | Phase |
|---|---|---|
| 1 | LipsEffect | 2 |
| 2 | IrisEffect | 6 |
| 3 | TeethEffect | 6 |
| 4 | BrowsEffect | 6 |
| 5 | UnderEyeEffect | 6 |
| 6 | HairThickenEffect | 6 |
| 7 | BeardThickenEffect | 6 |
| 8 | SkinSmoothEffect | 3 |
| (reserved) | 9-31 | future Bucket A |
| 32 | EyeEnlargeEffect | 4 |
| 33 | NoseReshapeEffect | 4 |
| 34 | LipPlumpEffect | 4 |
| 35 | FaceSlimEffect | 4 |
| (reserved) | 36-63 | future Bucket B |
| 64 | GlassesEffect | 5 |
| (reserved) | 65+ | future Bucket C / D / E |

IDs 1-31 are MaskedRecolor instances. IDs 32-63 are LandmarkWarp. 64+ are
AssetOverlay / ImageGrade / Background. Gap-based numbering means we can
add new variants in each bucket without ID collision.

### Decision 7: Where lip contour landmarks come from

**MediaPipe FaceMesh outer lip contour landmarks, with inner contour
subtracted when mouth is open.**

```
Outer upper lip (clockwise from right corner):
  61, 185, 40, 39, 37, 0, 267, 269, 270, 409, 291
Outer lower lip (continuing clockwise):
  291, 375, 321, 405, 314, 17, 84, 181, 91, 146, 61

Inner upper lip:
  78, 191, 80, 81, 82, 13, 312, 311, 310, 415, 308
Inner lower lip:
  308, 324, 318, 402, 317, 14, 87, 178, 88, 95, 78
```

The lip recolor mask is `outer ⊖ inner_if_mouth_open`. We detect mouth
openness via the existing `face.blendShapes[25]` (jawOpen) — if > 0.05,
the inner-lip contour gets subtracted from the mask.

This pattern (outer contour ⊖ optional inner contour) repeats for the iris
effect (iris contour ⊖ pupil if we model the pupil separately) and the
teeth effect (inner-mouth contour ⊕ teeth detection). Worth getting right.

### Decision 8: Per-effect perception requirements

**Each effect type declares which perception modules it requires. The graph
computes the union and pushes to `PerceptionPipeline::setRequirements()`.**

For LipsEffect: needs face landmarks. That's it. No iris, no hair, no
pose, no skin tone.

```cpp
// In MaskedRecolorEffect::perceptionInputs()
PerceptionInputs LipsEffect::perceptionInputs() const {
    PerceptionInputs in;
    in.needsFaceLandmarks = true;
    return in;
}
```

When we add IrisEffect in Phase 6, it adds `needsIrisLandmarks = true`.
The graph ORs everything together. PerceptionPipeline already knows how
to handle this — Phase 1 built that machinery.

---

## File-by-file plan

### New files (Phase 2 additions)

```
native/core/
├── effects/                                  ← NEW directory
│   ├── effect_base.h                         ← Abstract Effect interface
│   ├── effect_graph.{h,cpp}                  ← Graph orchestrator
│   ├── effect_types.h                        ← Type IDs + registry
│   ├── masked_recolor_effect.{h,cpp}         ← Bucket A engine
│   └── mask_rasterizer.{h,cpp}               ← Landmark → mask GPU pipeline
├── ffi/
│   └── community_ar_phase2_api.h             ← Phase 2 ABI additions
├── shaders/                                  ← NEW directory (or inline strings)
│   ├── masked_recolor.frag                   ← Oklab recolor shader
│   └── mask_rasterizer.{vert,frag}           ← Soft-edge mask rasterizer

lib/src/
├── effects/                                  ← NEW directory (Dart side)
│   ├── effect.dart                           ← Abstract Effect base class
│   ├── effect_graph.dart                     ← EffectGraph Dart wrapper
│   ├── lips_effect.dart                      ← LipsEffect public API
│   └── colors.dart                           ← Color utility (sRGB conversions)
└── ffi/
    └── community_ar_phase2_ffi.dart          ← Method channel surface
```

### Modified files

- `phase0_session.{h,cpp}` — Add `EffectGraph` member, swap test shader rendering
  for `effectGraph_->render()` in the per-frame path.
- `community_ar_phase0_api.h` — Add `car_p2_graph_set` and `car_p2_graph_clear`.
- `CMakeLists.txt` — Add `effects/*.cpp` and `mask_rasterizer.cpp` to sources.
- `example/lib/main.dart` — Replace Phase 1 perception debug UI with a
  lip-color picker that proves the end-to-end works.

### Files NOT modified

Phase 1 perception code is untouched. Phase 0 plumbing is untouched.
Phase 2 sits on top.

---

## API shapes

### Dart public API (`lips_effect.dart`)

```dart
class LipsEffect extends Effect {
  final Color color;
  final double opacity;
  final double edgeSoftness;
  final double luminancePreserve;

  const LipsEffect({
    required this.color,
    this.opacity = 0.85,
    this.edgeSoftness = 0.4,
    this.luminancePreserve = 1.0,
  });

  @override
  int get typeId => 1;  // matches the C ABI table

  @override
  Uint8List serialize() {
    // Pack into the CARLipsEffectConfig struct layout
    final bytes = ByteData(28);
    bytes.setUint32(0,  1, Endian.host);  // version
    bytes.setFloat32(4,  color.red   / 255.0, Endian.host);
    bytes.setFloat32(8,  color.green / 255.0, Endian.host);
    bytes.setFloat32(12, color.blue  / 255.0, Endian.host);
    bytes.setFloat32(16, opacity,            Endian.host);
    bytes.setFloat32(20, edgeSoftness,       Endian.host);
    bytes.setFloat32(24, luminancePreserve,  Endian.host);
    return bytes.buffer.asUint8List();
  }
}
```

Usage in a Flutter widget:

```dart
CommunityARView(
  camera: CameraLens.front,
  effects: EffectGraph(effects: [
    LipsEffect(color: Colors.red.withOpacity(0.85)),
  ]),
)
```

### C ABI surface

```c
// community_ar_phase2_api.h
#define CAR_EFFECT_TYPE_LIPS  1

typedef struct {
    uint32_t version;            // = 1
    float    colorR, colorG, colorB;
    float    opacity;
    float    edgeSoftness;
    float    luminancePreserve;
} CARLipsEffectConfig;

// Replace the entire graph atomically. Older graph is destroyed.
CAR_EXPORT CARStatus car_p2_graph_set(
    CARSession*       session,
    uint32_t          effect_count,
    const uint32_t*   effect_type_ids,
    const void*       const* configs,
    const size_t*     config_sizes);

CAR_EXPORT CARStatus car_p2_graph_clear(CARSession* session);
```

### C++ Effect interface

```cpp
// effect_base.h
class Effect {
public:
    virtual ~Effect() = default;

    virtual uint32_t typeId() const = 0;

    // What this effect needs from perception
    virtual PerceptionInputs perceptionInputs() const = 0;

    // Per-frame: rasterize masks, prepare per-effect state.
    // Called after PerceptionPipeline::run() each frame.
    virtual void prepare(const PerceptionFrame& frame,
                         RenderContext* ctx) = 0;

    // Per-frame: render this effect, reading from inputTex,
    // writing to outputFbo.
    virtual void render(const TextureHandle& inputTex,
                        Framebuffer* outputFbo,
                        RenderContext* ctx) = 0;
};
```

### MaskedRecolorEffect (the engine)

```cpp
// masked_recolor_effect.h
class MaskedRecolorEffect : public Effect {
public:
    struct Config {
        // Which landmark contour to use (FaceMesh indices)
        std::vector<int> outerContourIndices;
        std::vector<int> innerContourIndices;  // optional, may be empty

        // When the inner contour should be subtracted (blendshape index + threshold)
        // For lips: blendshape 25 (jawOpen) > 0.05
        int   innerSubtractBlendshape;
        float innerSubtractThreshold;

        // Recolor parameters (from the user-facing config)
        float colorR, colorG, colorB;
        float opacity;
        float edgeSoftness;
        float luminancePreserve;
    };

    explicit MaskedRecolorEffect(Config cfg);
    uint32_t typeId() const override { return cfg_.typeId; }  // set by subclass
    PerceptionInputs perceptionInputs() const override;
    void prepare(const PerceptionFrame&, RenderContext*) override;
    void render(const TextureHandle&, Framebuffer*, RenderContext*) override;

private:
    Config cfg_;
    std::unique_ptr<MaskRasterizer> rasterizer_;
    std::unique_ptr<TextureHandle>  maskTexture_;
    std::unique_ptr<ShaderProgram>  recolorShader_;
};

// lips_effect.cpp — just a factory
std::unique_ptr<Effect> makeLipsEffect(const CARLipsEffectConfig* cfg) {
    MaskedRecolorEffect::Config mc;
    mc.outerContourIndices = {61, 185, 40, /* ... */, 146, 61};
    mc.innerContourIndices = {78, 191, /* ... */, 95, 78};
    mc.innerSubtractBlendshape = 25;  // jawOpen
    mc.innerSubtractThreshold = 0.05f;
    mc.colorR = cfg->colorR;
    mc.colorG = cfg->colorG;
    mc.colorB = cfg->colorB;
    mc.opacity = cfg->opacity;
    mc.edgeSoftness = cfg->edgeSoftness;
    mc.luminancePreserve = cfg->luminancePreserve;
    return std::make_unique<MaskedRecolorEffect>(std::move(mc));
}
```

Every future Bucket A effect (IrisEffect, TeethEffect, etc.) is the same
shape: a factory function that pre-fills the contour indices for that body
region and forwards the user config. No subclassing required.

### EffectGraph

```cpp
// effect_graph.h
class EffectGraph {
public:
    explicit EffectGraph(RenderContext* ctx);

    // Replace all effects atomically
    void setEffects(std::vector<std::unique_ptr<Effect>> effects);

    // Union of all effects' perception requirements
    PerceptionInputs perceptionInputs() const;

    // Run all effects for this frame
    // inputCamera: the camera output texture
    // outputFbo: where the final composited result lands
    void render(const TextureHandle& inputCamera,
                const PerceptionFrame& frame,
                Framebuffer* outputFbo);

private:
    RenderContext* ctx_;
    std::vector<std::unique_ptr<Effect>> effects_;

    // Ping-pong FBOs for chaining effects (lazily allocated, sized to display)
    std::array<std::unique_ptr<Framebuffer>, 2> chainFbos_;
    std::array<std::unique_ptr<TextureHandle>, 2> chainTextures_;
};
```

---

## Shader design

### `masked_recolor.frag` (GLSL ES 3.00)

The core shader. ~50 lines. Bucket A's beating heart.

```glsl
#version 300 es
precision mediump float;

uniform sampler2D uInputFrame;
uniform sampler2D uMask;
uniform vec3      uTargetColor;     // sRGB, [0, 1]
uniform float     uOpacity;
uniform float     uLuminancePreserve;

in  vec2 vUv;
out vec4 fragColor;

// sRGB <-> linear and linear <-> Oklab. Standard formulas, ~20 lines each.
// (Inlined here in production; abstracted to helpers/oklab.glsl in the repo.)

vec3 srgbToLinear(vec3 c) { /* ... */ }
vec3 linearToSrgb(vec3 c) { /* ... */ }
vec3 linearToOklab(vec3 c) { /* ... */ }
vec3 oklabToLinear(vec3 c) { /* ... */ }

void main() {
    vec4 input  = texture(uInputFrame, vUv);
    float mask  = texture(uMask, vUv).r;          // 0 = no effect, 1 = full
    float a     = mask * uOpacity;
    if (a < 0.001) { fragColor = input; return; }

    // To Oklab
    vec3 srcLin = srgbToLinear(input.rgb);
    vec3 tgtLin = srgbToLinear(uTargetColor);
    vec3 srcLab = linearToOklab(srcLin);
    vec3 tgtLab = linearToOklab(tgtLin);

    // Preserve luminance per uLuminancePreserve, blend chroma per `a`
    float L = mix(tgtLab.x, srcLab.x, uLuminancePreserve);
    float aChan = mix(srcLab.y, tgtLab.y, a);
    float bChan = mix(srcLab.z, tgtLab.z, a);

    vec3 outLin = oklabToLinear(vec3(L, aChan, bChan));
    fragColor = vec4(linearToSrgb(outLin), input.a);
}
```

### `mask_rasterizer.{vert,frag}` (GLSL ES 3.00)

Rasterizes a soft-edged lip mask from a triangle fan. The trick: each
vertex carries an `aAlpha` attribute (1.0 for interior, 0.0 for boundary),
and the fragment shader smoothsteps that value.

```glsl
// vertex
#version 300 es
precision mediump float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in float aAlpha;
out float vAlpha;
void main() {
    gl_Position = vec4(aPos * 2.0 - 1.0, 0.0, 1.0);
    vAlpha = aAlpha;
}

// fragment
#version 300 es
precision mediump float;
uniform float uEdgeSoftness;  // 0..1, expands the soft band
in  float vAlpha;
out vec4  fragColor;
void main() {
    float t = smoothstep(0.0, 1.0 - uEdgeSoftness * 0.7, vAlpha);
    fragColor = vec4(t, 0, 0, 1);  // single-channel mask
}
```

---

## Performance budget

At 30fps we have 33ms. Phase 1 perception takes ~15-20ms on mid-range
Android. Phase 2 effect overhead target: **< 2ms total.**

Breakdown:
- Mask rasterization: ~0.3ms (small triangle fan, single FBO write)
- Recolor shader pass: ~0.5ms (fragment-shader heavy but full screen)
- Total per effect: ~0.8ms
- Two-FBO ping-pong setup: ~0.1ms

A single LipsEffect should be invisible in the frame budget. The architecture
supports stacking ~10 effects before becoming the bottleneck — at which point
Phase 3's mask-sharing optimization becomes worth doing.

---

## Verification checklist

### Functional
- [ ] `LipsEffect(color: red)` produces visibly red lips on the camera feed
- [ ] Changing `color` in Dart updates the live feed within one frame
- [ ] Lipstick stays attached to lips during head movement
- [ ] Mouth opens → teeth/tongue are NOT painted (inner contour subtraction works)
- [ ] Mouth closes → lipstick covers full lip area
- [ ] `opacity = 0` → no visible effect (degenerate case works)
- [ ] `opacity = 1, luminancePreserve = 0` → fully painted, looks like flat color
- [ ] `opacity = 0.7, luminancePreserve = 1` → natural lipstick look

### Quality
- [ ] Mask edges are soft, not polygonal — no visible faceting on the lip outline
- [ ] Works on dark, medium, and light skin tones with the same visual quality
  (Oklab math is doing its job — verify cross-tone)
- [ ] No color leakage onto chin/cheek/inside-of-mouth at default edge softness
- [ ] No temporal flicker on the mask edges when face is stationary

### Performance
- [ ] Total per-frame effect time < 2 ms on Snapdragon 7-class Android
- [ ] No frame rate drop vs Phase 1 baseline
- [ ] Adding the effect doesn't add visible latency to head-tracking response

### Architecture
- [ ] `EffectGraph` with one effect runs cleanly; replacing graph atomically works
- [ ] `LipsEffect.serialize() → CARLipsEffectConfig → C++` round-trip preserves
      all six fields
- [ ] `MaskedRecolorEffect` takes config — no LipsEffect-specific code in the
      engine itself (proves the engine is reusable for Phase 6 effects)
- [ ] Two faces in the camera → both get lipstick (multi-face mask works)
- [ ] Effect handles 0 faces gracefully (no crash when nobody's in frame)

---

## Open questions to resolve before coding

These should be settled in a single conversation before Phase 2 implementation
starts:

1. **Color space at the Dart API boundary.** Currently Dart's `Color` is sRGB
   8-bit per channel. Do we accept that and let Oklab conversion happen
   C++-side? Or accept an `OklabColor` type for power users? Recommendation:
   sRGB only at the API; Oklab is an implementation detail.

2. **Where do edge softness defaults live?** Per-effect (LipsEffect has its
   default, IrisEffect has its own)? Or a global preset? Recommendation:
   per-effect, since iris edge softness should be much tighter than lips.

3. **Should `EffectGraph` be a value type or a stateful object?** Dart-side,
   I'm proposing value type (immutable, replaces on change). C++-side it's
   stateful. Are we OK with that asymmetry? Recommendation: yes, the
   asymmetry is what lets the Dart API feel declarative while the C++ side
   keeps GPU resources alive across "rebuilds."

4. **Do we expose the lip contour landmark indices, or hide them entirely?**
   Recommendation: hide them. The user provides `color`, we know which
   landmarks to use. Phase 6 effects will be the same — `IrisEffect(color:)`
   doesn't expose iris landmark indices either.

5. **Animation hook for Phase 4.** Should the Config struct have a `t`
   parameter we plumb through now (saving us from changing the ABI in
   Phase 4)? Recommendation: NO. Phase 4 will introduce animation
   wholesale; adding a half-built version now risks designing the wrong
   abstraction.

---

## What Phase 2 unlocks for Phase 3+

After Phase 2 ships, the following effects become essentially one-day
additions each (just a factory function setting different contour indices):

- IrisEffect (color the iris)
- TeethEffect (whiten teeth — different blend math but same engine)
- BrowsEffect (color the brows)
- UnderEyeEffect (subtle brightening under the eyes)
- HairThickenEffect (uses the hair mask instead of a landmark contour)
- BeardThickenEffect (uses a beard mask or jawline contour)

Phase 3 (`SkinSmoothEffect`) is a separate engine (BeautyFilter), but it
plugs into the same `EffectGraph` as another effect with its own perception
inputs.

Phase 4 (LandmarkWarp engine) adds a parallel engine for the warp-based
effects, also plugging into the same `EffectGraph`.

In short: Phase 2 settles the load-bearing API. Phases 3-7 are mechanical
additions of new engines and effect types against this foundation.
