# Community AR Phase 3 — Effect composition & skin beautification

Two beauty filters compose in one graph. The user picks
`BeautyPresets.glamour` and a red lipstick; both effects run per frame,
beauty first then lipstick, on real perception data with tone-aware skin
math that works across light, medium, and dark skin.

This is where Community AR earns its name. The bones are now in place
for everything Phase 4+ will add: warp effects sit at `passOrder=Warp`,
3D overlays at `Overlay`, post-processing at `PostProcess`. Compose
freely; the graph figures out the order.

## What's new vs Phase 2

| Component | Purpose |
|---|---|
| `SegmenterBackend` (C++ abstract) | Swappable segmentation model — hair-only or multiclass |
| `MulticlassSegmenterBackend` | New: 6-channel output (background, hair, body-skin, **face-skin**, clothes, others) |
| `HairSegmenterBackend` | Phase 1 default, now behind the abstraction as fallback |
| `SegmenterBackendFactory` | Graceful selection with model-file existence checks |
| `EffectPass` enum | Gap-numbered ordering: BaseColorGrade < SkinAdjust < Recolor < Warp < Overlay < Background < PostProcess |
| `MaskResourcePool` | Named mask storage per-frame — canonical `masks.faceSkin`, `masks.hair`, etc. |
| `MaskRequirements` | Effects declare `consumes` / `produces` for the pool |
| `EffectGraph` v2 | Sorts effects by `passOrder`, owns the mask pool, splits prepare/render phases |
| `SkinSmoothEffect` (C++) | 9-pass beauty pipeline — frequency separation, tone-aware, temporal-stable |
| `BeautyFilterConfig` (Dart) | 13 user-facing knobs + quality tier |
| `BeautyPresets` | Nine pre-tuned looks: off, natural, subtle, glamour, softGlow, matte, editorial, lowLight, studio |
| `BeautyQuality` enum | Auto / High / Medium / Low — auto-resolved by startup benchmark |
| Beauty shader common (`beauty_shader_common.h`) | Shared Oklab helpers used by P3/P5/P5.5/P6 |
| Nine fragment shaders | P1 (mask refinement), P2 (downsample), P3 (bilateral), P4 (upsample), P5 (composition), P5.5 (specular), P6 (glow), P6.5 (temporal) |
| `RenderContext::createMRTFramebuffer` | Multi-render-target framebuffer — used by multiclass channel splitter |
| `RenderContext::drawTriangles` | Raw triangle draw (Phase 2 retroactive) |
| Phase 3 C ABI | `car_p3_beauty_effective_quality`, `car_p3_mask_pool_list`, `CARBeautyFilterConfig` |

## File map

```
native/core/
├── perception/
│   ├── segmenter_backend.h                        Abstraction
│   ├── hair_segmenter_backend.{h,cpp}             Phase 1 fallback
│   ├── multiclass_segmenter_backend.{h,cpp}       6-channel model
│   ├── segmenter_backend_factory.{h,cpp}          Picks with fallback
│   └── perception_pipeline_phase3_updates.cpp     Integration delta
├── effects/
│   ├── effect_pass.h                              Pass ordering enum
│   ├── mask_resource_pool.{h,cpp}                 Named mask pool
│   ├── effect_graph.{h,cpp}                       v2 — sorted, pool-aware
│   ├── effect_base_phase3_updates.h               Effect interface deltas
│   ├── skin_smooth_effect.{h,cpp}                 9-pass beauty engine
│   ├── beauty_shader_common.h                     Shared Oklab helpers
│   ├── beauty_shader_p1_skin_mask.h               Mask refinement
│   ├── beauty_shader_p2_downsample.h              13-tap Gaussian
│   ├── beauty_shader_p3_bilateral.h               Separable bilateral
│   ├── beauty_shader_p4_upsample.h                4-tap tent
│   ├── beauty_shader_p5_composition.h             Multi-band Oklab (the heart)
│   ├── beauty_shader_p55_specular.h               Matte/glow continuum
│   ├── beauty_shader_p6_glow.h                    Warmth/lift/clarity
│   └── beauty_shader_p65_temporal.h               History blend
├── render/
│   ├── render_context_additions.h                 (updated) MRT + drawTriangles
│   ├── gles_render_context_phase3_updates.cpp     GLES implementations
│   └── metal_render_context_phase3_updates.mm     Metal implementations
└── ffi/
    ├── community_ar_phase3_api.h                  Beauty config + diagnostics
    └── community_ar_phase3_api.cpp                Factory + tier query

lib/src/
├── effects/
│   ├── beauty_quality.dart                        BeautyQuality enum
│   ├── beauty_filter_config.dart                  13-knob config + validate()
│   ├── beauty_preset.dart                         Nine presets
│   └── skin_smooth_effect.dart                    User-facing Effect class
└── ffi/
    └── community_ar_phase3_ffi.dart               Phase 3 diagnostic accessors

example/lib/main.dart                              Preset picker + lipstick + tier badge
```

## Architectural invariants Phase 3 establishes

These are stable for v1; later phases extend without violating them.

1. **`EffectPass` is gap-numbered.** Adding a new pass category between
   existing ones means picking an integer in the gap — no renumbering.
2. **Mask names are canonical strings.** `MaskResourcePool::kFaceSkin`,
   `kHair`, `kBodySkin`, etc. — defined in one place, referenced
   everywhere.
3. **Effects declare requirements; the graph allocates.** An effect's
   `perceptionInputs()` and `maskRequirements()` are read at graph
   construction time; the graph configures perception and the mask pool
   to satisfy the union.
4. **`prepare()` runs for all effects before any `render()` runs.** This
   lets a later-passOrder effect produce a mask that an earlier-passOrder
   effect would consume — though in practice this is rare.
5. **The mask pool is per-frame.** Cleared at the start of every frame;
   producers re-populate; consumers read. No cross-frame mask sharing.
6. **Beauty quality tiers are auto-resolved on first activation.** Ten
   frames at High, then lock based on average measured frame time.
   Adaptive throttling adjusts thereafter if conditions change.
7. **Tone-awareness is automatic, not user-facing.** Shaders scale
   thresholds by `face.skinTone.baselineLuma`. The user never sees a
   "skin tone" knob.

## Cross-skin-tone correctness (THE differentiator)

The Phase 3 beauty pipeline was designed from the start to work across
skin tones. The mechanism is consistent across every pass:

- **Bilateral edge detection** uses Oklab L (perceptual luminance), not
  RGB. Same edge threshold behaves the same on dark and light skin.
- **Mid-band wrinkle attenuation** has a tone-aware floor: darker skin
  gets less aggressive smoothing (less mid-band texture to remove in
  the first place).
- **Highlight lift window** shifts down for darker skin so the lift kicks
  in where there's actually highlight to work with.
- **Specular detection threshold** scales by `baselineLuma` — a "highlight"
  on dark skin is at a lower absolute L value than on light skin.

The result: a single set of preset values produces visually consistent
beauty across the full range of skin tones, with no per-tone tuning.

This is the property that distinguishes Community AR from beauty
filters that look great in a small demographic range and increasingly
muddy outside it. **Verify it visually for every shader change.** See
`TESTING.md` § "Cross-skin-tone verification".

## What deliberately isn't in Phase 3

Scope-limited; named for future phases:

- **Per-face beauty configuration.** Multiple faces all use the
  baseline of the dominant face. Per-face customization is deferred to
  Phase 6+.
- **Quarter-resolution bilateral for Low tier.** Low tier currently runs
  bilateral at half-resolution like Medium. Implementation deferred to
  Phase 8 polish; revisit only if real-device profiling shows Low-tier
  devices struggling.
- **GPU timer queries.** The auto-tier benchmark uses wall-clock timing
  (`std::chrono`). Phase 5's Filament adoption brings proper GPU timing
  primitives; revisit then.
- **Hair effects.** The multiclass segmenter outputs a hair channel,
  but no effect consumes it in Phase 3. Phase 6 adds `HairThickenEffect`;
  Phase 7 adds 3D hairstyles via `AssetOverlayEffect`.
- **Background effects.** Multiclass segmenter outputs background and
  clothes channels; no effect consumes them in Phase 3. Phase 6 adds
  `BackgroundEffect`.
- **Landmark-derived mask sharing.** The lip mask is private to
  `MaskedRecolorEffect`. Sharing patterns become clear when Phase 6
  brings 6 more recolor effects; revisit then.

## Verification checklist

### Functional
- [ ] `SkinSmoothEffect(config: BeautyPresets.natural)` produces visibly
      smoothed skin while preserving pore detail
- [ ] Compositing with `LipsEffect` puts lipstick on beautified skin
      regardless of declaration order
- [ ] Switching presets produces visually distinct results
      (matte vs glow, glamour vs subtle)
- [ ] Specular slider transitions are smooth (~5-frame ease, not instant snap)
- [ ] `BeautyFilterConfig.validate()` throws on out-of-range values
- [ ] `quality: BeautyQuality.auto` resolves to High/Medium/Low within
      ~10 frames after activation
- [ ] Effective tier visible via `CommunityARPhase3FFI.getBeautyEffectiveQuality()`

### Quality (THE differentiator)
- [ ] **Works on dark, medium, and light skin tones with consistent
      visual quality** — verify before shipping ANY beauty shader change
- [ ] Skin smoothing reduces visible pores without "plastic skin" look
- [ ] Feature edges (lips, eyes, brows) stay crisp through beauty pass
- [ ] No color leakage onto hair / clothing / background
- [ ] Temporal stability: stationary face shows no "crawling skin"
- [ ] Disocclusion: rapid head movement doesn't ghost

### Performance
- [ ] High tier: 95th-percentile frame time < 22 ms on mid-range Android
- [ ] Medium tier: < 26 ms
- [ ] Low tier: < 30 ms
- [ ] Adaptive throttle drops a tier when conditions degrade
- [ ] Adaptive throttle restores after 100 frames of headroom
- [ ] Multi-face: 3+ faces in frame → auto drops High → Medium

### Architecture (proves the foundation is reusable)
- [ ] `EffectGraph.setEffects()` orders by `passOrder()` regardless of
      declaration order
- [ ] `MaskResourcePool` populated with `masks.faceSkin` when multiclass
      segmenter active
- [ ] `SkinSmoothEffect` publishes `masks.refinedFaceSkin` to the pool
      (produce-side API exercised end-to-end)
- [ ] Segmenter backend falls back to hair-only when multiclass model
      file missing
- [ ] `CARBeautyFilterConfig` round-trips through Dart ↔ C ABI with
      byte-exact field offsets
- [ ] `EffectGraph::findFirstEffectOfType()` returns the installed
      `SkinSmoothEffect`

## What Phase 3 unlocks

The infrastructure is now in place for the remaining buckets:

- **Phase 4 — LandmarkWarp engine.** New engine plugs in at
  `passOrder=Warp`. Eye enlarge, nose reshape, lip plump, face slim —
  all instances of the same warp engine, like Bucket A's recolor
  family.
- **Phase 5 — AssetOverlay engine (Filament).** New engine plugs in at
  `passOrder=Overlay`. Glasses, hats, earrings, 3D hairstyles —
  composited on top of the 2D pipeline.
- **Phase 5.5 — ImageGradeEffect.** Plugs in at `passOrder=BaseColorGrade`
  (before everything). LUT-based color grading; runs first so everything
  above samples the graded base.
- **Phase 6 — Bucket A completion + Background.** Iris, teeth, brows,
  under-eye, hair-thicken, beard-thicken effects each land as a
  factory function setting different contour indices. Background blur /
  replace plugs in at `passOrder=Background`.

Each is a focused addition against the Phase 3 foundation, not a
re-architecture. The hardest decisions are already made.
