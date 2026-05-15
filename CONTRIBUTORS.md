# Community AR — Contributors guide

Welcome. We're glad you're here.

CommunityAR is a genuine open source project, built on the premise that the best face AR library is one shaped by the global engineering community.

The project's roadmap is clearly defined and structured into discrete milestones, and its successful delivery depends on contributor effort. Your time is valuable, and this document has been written in respect of that in mind.

## What you're walking into

A few honest things about this codebase before you invest in it:

**It's opinionated.** The architecture has strong commitments — Oklab color math, per-track state, zero-copy GPU pipelines, three effect engines instead of per-feature classes. These exist because the alternatives are worse for the project's goals, not because of inertia. [`CLAUDE.md`](CLAUDE.md) is the operating manual for these decisions; please read it before proposing significant changes.

**It's pre-1.0.** APIs will change. Internal code will be refactored. We mark stable boundaries (the C ABI, the public Dart API) and treat everything else as work in progress. If you build on top, expect to update your code when we cut new versions.

**It's verbose by design.** Files open with multi-paragraph "why this file exists, what tensions it resolved" comments. This is deliberate — it's slow to write and fast to read, which is the right trade-off for a codebase meant to be modified by many hands.

**Quality is real.** We test on diverse faces and devices because the alternative produces visibly worse output for half the world. "Works on my iPhone" isn't done; the verification checklists in `docs/PHASE_*.md` are.

**The maintainer team is small.** Reviews may take a few days. We try to give substantive feedback on every PR but sometimes that means waiting for the right reviewer. If you don't hear back in a week, ping the PR.

## What contributions matter the most

- **Device testing.** The gap between this library and polished closed alternatives is often breadth of device testing. If you have an Android or iOS device and a face, you can help.
- **Shader work.** The effect engines are GLSL ES 3.00 + 3.10. If you've written a fragment shader before, you can ship an effect.
- **On-device ML.** Improving segmentation quality is on the critical path for several phases.
- **Documentation and tutorials.** Particularly tutorials aimed at first-time Flutter developers and those new to AR.
- **Design.** What does a well-curated effect catalog look like? We need design thinking, not just engineering.

## Where we are at
CommunityAR is at **pre-1.0** version and developed in public. APIs may change up to 1.0; semantic versioning kicks in after that. The `master` branch reflects current development; tagged releases are stable snapshots.

Here are the current available and well tested library capabilities:

| Capability | Status |
|---|---|
| Camera → GPU → Flutter pipeline (zero-copy where possible) | ✅ Shipped |
| Face landmarks (468 points, MediaPipe) with One-Euro temporal smoothing | ✅ Shipped |
| Multi-face tracking with stable IDs across frames | ✅ Shipped |
| Iris detection — both eyes, per-track filtering | ✅ Shipped |
| Hair and selfie segmentation | ✅ Shipped |
| 6DoF face pose (custom PnP, no OpenCV dependency) | ✅ Shipped |
| Skin tone estimation via GPU compute + async readback | ✅ Shipped |
| **LipsEffect — first end-to-end effect** | ✅ Shipped |


Not yet recommended for production use unless you're comfortable reading and modifying the source. We're getting there steadily.

## Roadmap

| Phase | What |
|---|---|
| 0 | Plumbing — ensuring the zero-copy pipeline works on both platforms at real frame rates.
| 1 | The perception layer — every frame produces a complete `PerceptionFrame`
| 2 | First effect (Lips recolor): Implementing, testing and validating `LipsEffect`
| 3 | Effect graph composition + multi-band skin beautification |
| 4 | LandmarkWarp engine — eye enlarge, nose reshape, lip plump, face slim |
| 5 | Filament integration — 3D accessories (glasses, hats, earrings) with PBR |
| 5.5 | LUT-based color grading (film looks, B&W, vintage) |
| 6 | Iris / teeth / brows / under-eye / hair-thicken / beard-thicken + background effects |
| 7 | 3D hairstyles |
| 8 | Polish, cross-device testing, comprehensive docs |

Detailed phase plans in [`docs/`](docs/).

## Architecture in 60 seconds

```
Flutter (Dart)
     │ method channel
Platform adapter (Kotlin / Swift)
     │ JNI / ObjC++
C ABI (stable boundary)
     │
C++ core (80% of the code)
   ├── Perception (MediaPipe Tasks)
   ├── Three effect engines
   │     • MaskedRecolor (lips, iris, teeth, brows, hair-thicken, …)
   │     • LandmarkWarp (eye enlarge, nose reshape, …)
   │     • AssetOverlay (3D glasses, hats, hair via Filament)
   └── RenderContext (GLES / Metal)
```

Pixels stay GPU-resident from camera capture through final composite. Dart sees texture IDs, configuration structs, and event callbacks — never raw image data. The C ABI is the stability boundary; everything beneath it is free to evolve.

Full writeup: [`docs/PHASE_0.md`](docs/PHASE_0.md) through [`docs/PHASE_2.md`](docs/PHASE_2.md).

### Quick orientation

| If you want to... | Start here |
|---|---|
| Add a new recolor effect (lipstick, iris, etc.) | [`native/core/effects/lips_effect.cpp`](native/core/effects/lips_effect.cpp) — copy the pattern with different landmark indices |
| Improve mask edge quality | [`native/core/effects/mask_rasterizer.cpp`](native/core/effects/mask_rasterizer.cpp) |
| Tune color math | [`native/core/effects/masked_recolor_effect.cpp`](native/core/effects/masked_recolor_effect.cpp) — the Oklab shader is inline |
| Improve perception (landmarks, masks) | [`native/core/perception/`](native/core/perception/) |
| Run the verification checklist | [`docs/PHASE_2.md`](docs/PHASE_2.md) |


## Engineering highlights

A few decisions worth surfacing because they're what differentiates this library from the obvious alternatives:

- **Oklab color space for all recolor math.** RGB blending produces inconsistent results across base colors; Oklab is perceptually uniform, so a numeric blend produces the same visual shift regardless of the underlying pixel. Lipstick reads correctly on any face. The shader is inline in `masked_recolor_effect.cpp` (~50 lines).

- **Three engines, twenty+ effects.** `MaskedRecolorEffect`, `LandmarkWarpEffect`, and `AssetOverlayEffect` compose to produce every user-facing feature. Adding "TeethWhitening" is a ten-line factory function, not a new engine.

- **One-Euro adaptive temporal filtering** on every landmark output. Sub-pixel jitter is what makes naive face AR look amateur; filtering it out (with adaptive cutoff to preserve responsiveness during motion) is what makes the difference between "tech demo" and "ships in production."

- **Per-track state keyed by stable face IDs.** Filters, motion estimates, skin tone — all keyed by stable IDs from a Hungarian-IoU tracker. Two faces in the same frame don't scramble each other's state when they reorder between detections.

- **Async GPU readback.** Skin tone estimation runs as a compute pass with readback polled across 1-3 frames so the render thread never stalls.

- **Real device testing on diverse faces is part of "done."** Every effect's verification checklist includes testing across skin tones and hair textures. This produces better quality for all users — color math that works on darker skin invariably also produces more accurate results on lighter skin, because the underlying perceptual rigor reduces failure modes everywhere.


## How to find your first contribution

You don't need permission to start working — but you might save time by skimming open issues for the `good first issue` and `help wanted` labels first. If you want to propose something not currently tracked, open a Discussions thread before writing code.

## Match by skill and interest

| If you have... | Try... |
|---|---|
| A Flutter app and curiosity | Run the example app, report what breaks on your device |
| GLSL shader experience | Pick a planned Bucket A effect (iris, teeth, brows) — each is ~70 lines |
| C++/graphics background | Improve `MaskRasterizer` (handle concave polygons properly), or extend `RenderContext` |
| ML / MediaPipe familiarity | Help us validate or improve segmentation quality on diverse hair textures |
| Design sense | Propose the v1 effect catalog — what looks should the library ship with by default? |
| Technical writing | Tutorials, especially first-time-Flutter and first-time-AR ones |
| A device we don't have | Run the verification checklist, file detailed reports |

## The fastest meaningful contribution

If you want to ship something useful in your first hour: **run the example app on a real device, and file a detailed issue about how it performs**. Specifically:

- What device, OS version, and Flutter version
- Frame rate (visible from the debug overlay)
- Lipstick quality across the palette swatches
- Anything that looks wrong, jittery, or doesn't match the demo on other devices

Device coverage is the single biggest gap between this library and polished closed-source alternatives. Filling it doesn't require coding — just attention.

## How to propose larger changes

For anything bigger than a small fix:

1. **Open a Discussions thread first.** Describe what you want to build and why. Tag a maintainer if you want fast feedback.
2. **Wait for at least one ack.** This is for your benefit — we'd rather catch architectural issues before you've spent a weekend on a PR we'd ask you to rewrite.
3. **Reference the discussion in your PR description.**

This isn't bureaucracy. It's because the codebase has invariants (see [`CLAUDE.md`](CLAUDE.md)) that aren't obvious from reading code alone, and we want your time to land where it matters.

## How we work

### Pull requests

- Small, focused PRs review faster than large ones. If you're touching more than ~500 lines, consider splitting.
- Write the PR description like a story: *what changed, why, what alternatives you considered, what to look at first.*
- Add tests when adding behavior. Add or update verification checklist items when adding effects.
- New effects aren't merged until they've been tested on real faces — at minimum two skin tones and two hair textures. We can help with this if you don't have the devices/faces yourself.

### Code review

- We review for: architectural fit, performance, code clarity, test coverage, and quality across diverse inputs.
- Reviewer comments are suggestions unless they say `blocking:`. Discussion is welcome.
- Reviewers should be specific and kind. If you've been on the receiving end of toxic open source culture, you won't get it here. If you find yourself acting as a reviewer, hold that line.

### Communication

- **GitHub Issues** for concrete bugs and feature requests.
- **GitHub Discussions** for design questions, "is this a good idea?" conversations, and showing off what you've built.
- **PR threads** for everything related to a specific change.

We try to communicate in writing in public channels by default. Private DMs about the project happen when needed but the default is public so the rest of the community can learn from the conversation.


### Style and conventions

- C++17. `#pragma once` headers. No exceptions in render-thread paths. See [`CLAUDE.md`](CLAUDE.md) for the full conventions doc.
- On Dart code: `dart format` on every commit.
- Comments explain *why*, not *what*. The codebase favors deliberate top-of-file commentary describing the design tensions each file resolved.
- Don't break the C ABI without a discussion.
- Shaders: GLSL ES 3.00 for fragment, 3.10 for compute. Inline in C++ for now; SPIRV-Cross unification lands in Phase 5.


See [`CLAUDE.md`](CLAUDE.md) for the codebase's operating principles and conventions — it's written for AI coding agents but is a great fast read for humans. It captures *how this codebase wants to be modified*.

## What we won't accept

Being upfront about these saves everyone's time:

- **AI-generated PRs without human review.** AI tools are fine for drafting, but submit only what you've personally read, tested, and stand behind. PRs that read as unverified bulk-generated code will be closed.
- **Drive-by performance "optimizations" without measurement.** Bring numbers.
- **Style-only changes to existing code.** If you find code that bothers you, open an issue first.
- **Changes that improve one face/skin tone/hair texture at the expense of others.** Quality is universal here; trade-offs need to be explicit and justified.
- **Behavior that's hostile to other contributors.** This includes condescension, gatekeeping, and pretending that asking questions is somehow inferior to "figuring it out yourself." We have no patience for any of it.

## Recognition

Contributors are credited in release notes. Significant contributions earn co-authorship on the relevant commits. Major and sustained contributors are invited to join the maintainer team — this happens organically based on demonstrated judgment, not by application.

## Code of conduct

We use the [Contributor Covenant](https://www.contributor-covenant.org/version/2/1/code_of_conduct/) v2.1. Short version: be kind, assume good faith, and treat each other as colleagues. The maintainers enforce it.

## Discussion

GitHub Issues for bugs and feature requests. GitHub Discussions for design questions, "is this a good idea?" conversations, and showing off what you've built.

## License

By contributing, you agree that your contributions are licensed under Apache 2.0 (the same license as the project).

---

## A note on motivation

If you're contributing to this project, you probably care about face AR working well — not just for the use cases that show up in research papers, but for the use cases real people have in real apps.

The library is built openly because the best version of it is one that many people contribute to. Closed proprietary SDKs optimize for the markets their owners care about. An open library can optimize for whatever its contributors collectively bring. That's the bet. Help us prove it.

Whatever you bring — code, tests, docs, design, device coverage, honest bug reports — thank you for being here.
