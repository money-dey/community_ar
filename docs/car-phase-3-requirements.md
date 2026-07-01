# Phase 3 — Upfront requirements

Decisions to lock before starting Phase 3 (effect graph + skin beautify v2 port).

## Model choice: replace dedicated hair segmenter with multiclass selfie segmenter

**Recommendation:** swap `hair_segmenter.tflite` for `selfie_multiclass_256x256.tflite`.

### The two options

| | `hair_segmenter.tflite` | `selfie_multiclass_256x256.tflite` |
|---|---|---|
| Size | 5.0 MB | 15.6 MB |
| Output | 1 channel (hair) | 6 channels |
| Channels | hair | background, hair, body-skin, face-skin, clothes, others |
| Inference cost | 1 model pass | 1 model pass (same — output is wider, not deeper) |

### Why the swap matters for Phase 3

Skin beautify v2 needs a **tight skin mask** for the multi-band frequency separation passes. The current Phase 1 approach (landmark-derived mask from FaceMesh contours) is approximate — it includes lips, eyes, brows, nostrils unless they're explicitly subtracted, and it has hard edges that visibly seam where smoothing meets non-smoothing regions.

The multiclass model's **face-skin** channel solves three Phase 3 problems at once:

1. **Better skin mask.** Per-pixel neural segmentation produces a soft mask aligned with actual skin pixels, not a polygonal approximation. Mask edges feather naturally.

2. **Face-only beautification.** The **body-skin** channel is separate from face-skin. The beauty pipeline can target *only* face-skin and leave arms/neck untouched. Without this, when the user's neck or shoulders are visible, smoothing applies to them too — usually fine, but produces visible artifacts where skin meets clothing.

3. **Hair channel for free.** Phase 6 (hair-thicken effect) and Phase 7 (3D hairstyles) both need a hair mask. With the multiclass model, that's already in the output — no need to keep `hair_segmenter.tflite` around.

### Trade-off honestly accounted

- **+10 MB binary cost.** The 25-30 MB base app size budget can absorb this — current model totals are ~9 MB without it, so we'd land at ~19 MB even after the swap. Within budget.
- **One model handles three jobs.** Net effect on the perception pipeline is *fewer* models running, not more. We drop `hair_segmenter` entirely; the multiclass model takes its slot.

### URL

```
https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_multiclass_256x256/float32/latest/selfie_multiclass_256x256.tflite
```

Update `tools/fetch_models.sh` to swap this in when Phase 3 starts.

## Architectural implications for the effect graph

If the multiclass swap is locked in, the effect graph design in Phase 3 should:

- Treat segmentation channels as **first-class named mask resources** the graph can route to effects: `masks.faceSkin`, `masks.bodySkin`, `masks.hair`, `masks.clothes`. Effects declare which channels they consume; the graph wires them automatically.

- Run the multiclass inference **once per frame** regardless of how many effects consume its channels. The graph's resource-dedup pass handles this — same pattern as Phase 1's `PerceptionInputs` union.

- Cache the channel textures so re-reading is free. The model outputs all 6 channels in one pass; we should keep all 6 alive in case downstream effects (skin beautify, hair thicken, future clothing recolor) need different ones.

## What to verify before committing

Before swapping the model in production:

- [ ] Multiclass model inference time on mid-range Android (Snapdragon 7-class) ≤ 10 ms — slightly slower than dedicated hair segmenter but acceptable
- [ ] Multiclass mask quality on diverse skin tones (light → dark) — the dedicated hair segmenter was known to underperform on darker skin; verify the multiclass model doesn't regress here
- [ ] Multiclass mask quality on diverse hair textures (4A-4C, locs, braids) — same concern, must verify
- [ ] face-skin channel correctness when subject wears glasses, has facial hair, or wears makeup — these are common edge cases

If any of these fail, fall back to the dedicated `hair_segmenter.tflite` plus the landmark-derived skin mask we already have. The decision is not one-way.
