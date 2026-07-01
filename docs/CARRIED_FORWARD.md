# Carried-forward items

Deferred technical items that don't belong in any single `car-phase-N.md`
document but need to survive across sessions. Each entry names *where*
the fix eventually lands and *when* to actually do the work.

If you're picking up this project after time away — or if you're
Claude Code being invoked on this repo for the first time — this is
the list of "things we agreed to postpone." It is not a wishlist and
not a bug tracker. Everything here has been consciously deferred with
a documented rationale.

---

## Phase 5 tasks (do when Filament lands)

### Replace wall-clock benchmark with GPU timer queries
**Origin:** Phase 3 Batch 5.
**Current state:** `SkinSmoothEffect`'s auto-tier benchmark uses
`std::chrono::steady_clock` to measure render time. This measures the
CPU-side wall-clock duration of the render() call, which is reasonably
correlated with GPU cost (through driver back-pressure) but not exact.
**What to change:** Wire up `GL_TIME_ELAPSED` queries on GLES and
`MTLCounterSampleBuffer` on Metal. Feed the GPU-measured time into the
existing benchmark and adaptive-throttle logic — the surrounding tier-
selection math doesn't need to change.
**Why Phase 5:** Filament's integration brings these primitives as
part of its cross-platform timing abstraction. Building our own
platform-specific timer wrappers now would be work we throw away.
**Skip if:** Real-device data shows the wall-clock proxy is producing
the right tier decisions. Only do the work if we can point to a case
where it's misleading.

### Whole-frame auto-tier benchmark awareness
**Origin:** Phase 4 requirements risk analysis.
**Current state:** The benchmark measures only `SkinSmoothEffect::render()`
time, not the whole-frame time. When Phase 4 warps add cost, the beauty
tier won't know to adjust.
**What to change:** Move the benchmark measurement out of
`SkinSmoothEffect` into the `EffectGraph` render loop so it sees the
total per-frame cost.
**Why Phase 5:** Naturally paired with the GPU timer query work; both
touch the same benchmark code path.

---

## Phase 6 architectural decision (revisit when the effects land)

### MaskRasterizer sharing pattern
**Origin:** Phase 3 Batch 2, memorialized when we asked "does
`MaskRasterizer` publish to the pool now, or stay private?"
**Decision made:** Stay private to `MaskedRecolorEffect` for Phase 3.
Sharing patterns become clear when Phase 6 brings iris, teeth, brows,
under-eye, hair-thicken, and beard-thicken effects — at that point we
have six effects that might benefit from shared landmark-derived mask
production.
**What to revisit:** When implementing Phase 6, evaluate whether
`MaskRasterizer` should publish outputs like `masks.lipsContour`,
`masks.eyeContour`, etc. to the pool for downstream consumers. The
architectural surface for this exists (`produces` on `maskRequirements()`)
— the question is only whether the actual sharing is worth doing.
**Rationale for deferral:** Premature abstraction. Phase 3's produce-
side API is exercised via `SkinSmoothEffect` publishing
`masks.refinedFaceSkin`; the contract is proven end-to-end. Adding a
second producer with only one consumer doesn't teach us anything about
the right sharing pattern.

---

## Phase 8 polish tasks

### Quarter-resolution bilateral for Low quality tier
**Origin:** Phase 3 Batch 5.
**Current state:** Low tier runs the bilateral passes at half-res
like Medium, because texture sizes are allocated in `ensureResources()`
before the tier resolves. Low currently lands at ~4-5 ms instead of the
targeted 3 ms.
**What to change:** Either reallocate the half-res textures at quarter-
res on tier change (allocation jitter visible as one bad frame), or
always allocate a third set of quarter-res buffers alongside the half-
res ones and switch which set the shaders sample from based on tier.
The second approach is uglier but avoids the frame hiccup.
**Skip if:** Real-device profiling on Low-tier devices (Pixel 4a-class,
iPhone X-class) shows the current implementation stays within budget.
The optimization exists to serve a specific device class; without
evidence that class is struggling, don't bother.

### Log abstraction cleanup
**Origin:** Phase 3 Batch 1 note.
**Current state:** The perception code uses `std::cerr` directly for
diagnostic messages (log failures during segmenter backend init, etc.).
Per `CLAUDE.md`, native diagnostic logging should go through
`__android_log_print` / `os_log`.
**What to change:** Introduce a small logging abstraction in
`native/core/util/log.h` that dispatches to the right platform API.
Replace all `std::cerr` and `std::cout` usage in the native code.
**Skip conditions:** None. This is unambiguous cleanup, just not
release-blocking.

---

## Verification items pending real-device data

These aren't code changes — they're checks that were architecturally
committed to but haven't been performed against real hardware.

### Multiclass segmenter quality on diverse hair textures
**Origin:** Phase 3 Batch 1 verification gate M7.
**Why it matters:** The multiclass model is the Phase 3 default. If
its hair-channel quality is worse than the dedicated hair segmenter's
on 4A-4C hair, locs, or braids, we should adjust the default backend
selection (or the fallback logic).
**What "done" looks like:** Screenshots of the multiclass hair channel
on at least six subjects spanning hair textures 1A through 4C, plus
locs and braids. Documented in `docs/VERIFICATION_M7.md` at the time
the check is performed.

### One-Euro filter parameter validation on real motion
**Origin:** Phase 1 delta.
**Why it matters:** The filter's `min_cutoff` and `beta` parameters
were picked from theory and MediaPipe's published values; they haven't
been validated against real device data with a real subject's head
motion. Wrong values here produce either visibly jittery landmarks
(too little smoothing) or visibly lagged landmarks (too much).
**What "done" looks like:** A short recording of the debug overlay with
a subject making natural head movements; a subjective judgment that
landmarks stay locked to features without visible lag. Documented in
the PR that captures this.

### `FaceData.motion` field name assumption
**Origin:** Phase 3 Batch 5 verification pass caught two field-name
bugs but this one wasn't caught because the code compiles either way.
**What we assumed:** `face.motion` is a flat `float` on `FaceData`,
already normalized by face size.
**Why to verify:** If the canonical `perception_frame.h` has evolved
between chat sessions and now has `face.motion.magnitude` or
`face.motionVector.length()`, our code will produce whatever `float`
that expression happens to yield — probably wrong, but not a compile
error.
**What "done" looks like:** Confirmed by reading the current
`perception_frame.h` before the first real-device run, or via a
compile-time assertion in `skin_smooth_effect.cpp` that
`std::is_same_v<decltype(face.motion), float>`.

---

## How to use this document

**When picking up the project after time away:** Read this first. It
tells you what's been consciously deferred vs. what might be missing.

**When Claude Code first runs in this repo:** `CLAUDE.md` should
reference this file so Claude Code knows to consult it when planning
work in the affected phases.

**When adding new deferred items:** Include origin (which phase/batch
raised it), current state, what change is required, and either a
concrete "skip if" condition or an explicit "will definitely do this."
Vague TODOs get stale; specific ones don't.

**When removing items:** Move them to the phase document where they
landed, with a note in the PR that closes them. Don't just delete —
losing the rationale for why we deferred loses information.
