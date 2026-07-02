# Community AR

**Open-source face AR for Flutter.** Beauty filters, hair effects, accessories. Production-grade quality in a ~25 MB library. Android and iOS.

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Android%20%7C%20iOS-lightgrey.svg)]()
[![Flutter](https://img.shields.io/badge/Flutter-3.0%2B-027DFD.svg)](https://flutter.dev)

> **Picking up the project? Start with [`docs/START_HERE.md`](docs/START_HERE.md)** —
> current build/bring-up state, what to work on next, and how to resume.

---

Add real-time face effects to any Flutter app in three lines of Dart. The library handles camera plumbing, face perception, face warping, GPU rendering, and effect composition — exposing a declarative API that fits naturally into Flutter's widget tree.

Most face AR libraries are either closed-source SDKs with restrictive terms or research-grade projects without production polish. Community AR aims for both: open under Apache 2.0, *and* engineered with the rigor that real apps need — zero-copy GPU pipelines, perceptually uniform color math, multi-face tracking with stable IDs, sub-2ms effect overhead.

The library targets Flutter developers but most of the work lives in C++/GLES/Metal. Contributors with shader, ML, graphics, or design backgrounds are warmly welcomed — including those who've never written a line of Flutter.

## Usage

```dart
import 'package:community_ar/community_ar.dart';

CommunityARView(
  camera: CameraLens.front,
  effects: EffectGraph(effects: [
    SkinSmoothEffect(config: BeautyPresets.natural),
    LipsEffect(color: Color(0xFFCC0033)),
  ]),
)
```

Two effects, composed declaratively. The graph runs beauty before lipstick
regardless of the order you wrote them in. See `example/` for a runnable
demo with preset picker, palette, and live parameter sliders.

## What's here today

| Capability | Status |
|---|---|
| Camera → GPU → Flutter pipeline (zero-copy where possible) | ✅ Shipped |
| Face landmarks (468 points, MediaPipe) with One-Euro temporal smoothing | ✅ Shipped |
| Multi-face tracking with stable IDs across frames | ✅ Shipped |
| Iris detection — both eyes, per-track filtering | ✅ Shipped |
| Hair and selfie segmentation | ✅ Shipped |
| Multiclass segmentation (background / hair / body-skin / **face-skin** / clothes) | ✅ Shipped |
| 6DoF face pose (custom PnP, no OpenCV dependency) | ✅ Shipped |
| Skin tone estimation via GPU compute + async readback | ✅ Shipped |
| **LipsEffect — first end-to-end effect** | ✅ Shipped |
| **Effect graph composition with pass ordering + shared mask pool** | ✅ Shipped |
| **SkinSmoothEffect — 9-pass beauty pipeline, tone-aware across skin tones** | ✅ Shipped |
| **Auto-tier quality selection with adaptive throttling** | ✅ Shipped |

## Roadmap
The project is broken down into 9 phases as recapitulated in the following table.

| Phase | What |
|---|---|
| 0 | Foundations — Camera-to-Flutter GPU pipeline |
| 1 | Perception — Face landmarks, iris, segmentation, pose, and skin tone |
| 2 | First effect implementation — Lip recoloring end-to-end |
| 3 | Composition and beautification — Multi-effect graphs and the skin beauty pipeline |
| 4 | LandmarkWarp engine — eye enlarge, nose reshape, lip plump, face slim |
| 5 | Filament integration — 3D accessories (glasses, hats, earrings) with PBR |
| 5.5 | LUT-based color grading (film looks, B&W, vintage) |
| 6 | Iris / teeth / brows / under-eye / hair-thicken / beard-thicken + background effects |
| 7 | 3D hairstyles |
| 8 | Polish, cross-device testing, comprehensive docs |

Detailed phase plans in [`docs/`](docs/).

## Engineering highlights

A few decisions worth surfacing because they're what differentiates this library from the obvious alternatives:

- **Oklab color space for all recolor math.** RGB blending produces inconsistent results across base colors; Oklab is perceptually uniform, so a numeric blend produces the same visual shift regardless of the underlying pixel. Lipstick reads correctly on any face. The shader is inline in `masked_recolor_effect.cpp` (~50 lines).

- **Three engines, twenty+ effects.** `MaskedRecolorEffect`, `LandmarkWarpEffect`, and `AssetOverlayEffect` compose to produce every user-facing feature. Adding "TeethWhitening" is a ten-line factory function, not a new engine.

- **One-Euro adaptive temporal filtering** on every landmark output. Sub-pixel jitter is what makes naive face AR look amateur; filtering it out (with adaptive cutoff to preserve responsiveness during motion) is what makes the difference between "tech demo" and "ships in production."

- **Per-track state keyed by stable face IDs.** Filters, motion estimates, skin tone — all keyed by stable IDs from a Hungarian-IoU tracker. Two faces in the same frame don't scramble each other's state when they reorder between detections.

- **Async GPU readback.** Skin tone estimation runs as a compute pass with readback polled across 1-3 frames so the render thread never stalls.

- **Real device testing on diverse faces is part of "done."** Every effect's verification checklist includes testing across skin tones and hair textures. This produces better quality for all users — color math that works on darker skin invariably also produces more accurate results on lighter skin, because the underlying perceptual rigor reduces failure modes everywhere.

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

Full writeup: [`docs/car-phase-0.md`](docs/car-phase-0.md) through [`docs/car-phase-2.md`](docs/car-phase-2.md).

## Getting started

### Try the example app

```bash
git clone https://github.com/<you>/community_ar
cd community_ar
bash tools/fetch_models.sh                       # ~12 MB of MediaPipe models
cd example
flutter pub get
flutter run                                       # on a physical device — camera required
```

### Add it to your Flutter app

Once published to pub.dev (currently pre-release):

```yaml
dependencies:
  community_ar: ^0.1.0
```

Until then, depend on this repository directly:

```yaml
dependencies:
  community_ar:
    git:
      url: https://github.com/<you>/community_ar
```

Then the three-line snippet from the top of this README is a working app.

## Why "Community"?

Two reasons.

First, the library is built openly because the contributors who care most about face AR working well — across every face shape, skin tone, hair texture, and accessory style — aren't well served by closed proprietary SDKs. We want the people closest to the problems to contribute directly: as engineers, testers, model trainers, and designers of the effect catalog.

Second, "community" describes the project's working style. The library is opinionated and well-documented because we want contributions, not just consumption. The codebase is structured to make adding effects, improving perception, and tuning quality straightforward enough that you can ship a useful PR on your first day.

## Contributing

**Contributions are warmly welcomed.** The roadmap is substantial and getting it shipped depends on engineers, testers, and designers from many backgrounds.

### What kinds of contributions matter most right now

- **Device testing.** The gap between this library and polished closed alternatives is often breadth of device testing. If you have an Android or iOS device and a face, you can help.
- **Shader work.** The effect engines are GLSL ES 3.00 + 3.10. If you've written a fragment shader before, you can ship an effect.
- **On-device ML.** Improving segmentation quality is on the critical path for several phases.
- **Documentation and tutorials.** Particularly tutorials aimed at first-time Flutter developers and those new to AR.
- **Design.** What does a well-curated effect catalog look like? We need design thinking, not just engineering.

### Quick orientation

| If you want to... | Start here |
|---|---|
| Add a new recolor effect (lipstick, iris, etc.) | [`native/core/effects/lips_effect.cpp`](native/core/effects/lips_effect.cpp) — copy the pattern with different landmark indices |
| Improve mask edge quality | [`native/core/effects/mask_rasterizer.cpp`](native/core/effects/mask_rasterizer.cpp) |
| Tune color math | [`native/core/effects/masked_recolor_effect.cpp`](native/core/effects/masked_recolor_effect.cpp) — the Oklab shader is inline |
| Improve perception (landmarks, masks) | [`native/core/perception/`](native/core/perception/) |
| Run the verification checklist | [`docs/car-phase-2.md`](docs/car-phase-2.md) |

### Code style and conventions

- C++17. `#pragma once` headers. No exceptions in render-thread paths.
- Dart formatted via `dart format`.
- Shaders: GLSL ES 3.00 for fragment, 3.10 for compute. Inline in C++ for now; SPIRV-Cross unification lands in Phase 5.
- Comments explain *why*, not *what*. The codebase favors deliberate top-of-file commentary describing the design tensions each file resolved.

See [`CLAUDE.md`](CLAUDE.md) for the codebase's operating principles and conventions — it's written for AI coding agents but is a great fast read for humans. It captures *how this codebase wants to be modified*.

### Discussion

GitHub Issues for bugs and feature requests. GitHub Discussions for design questions, "is this a good idea?" conversations, and showing off what you've built.

## Status & expectations

Community AR is **pre-1.0** and developed in public. APIs may change up to 1.0; semantic versioning kicks in after that. The `master` branch reflects current development; tagged releases are stable snapshots.

Not yet recommended for production use unless you're comfortable reading and modifying the source. We're getting there steadily.

## License

Apache 2.0 — see [LICENSE](LICENSE). Bundled MediaPipe models are also Apache 2.0; see [`licenses/MEDIAPIPE_LICENSE.txt`](licenses/MEDIAPIPE_LICENSE.txt) for attribution requirements.

## Acknowledgements

- Face perception models from [Google's MediaPipe](https://ai.google.dev/edge/mediapipe).
- Oklab perceptual color space by [Björn Ottosson](https://bottosson.github.io/posts/oklab/).
- The One-Euro filter (Casiez, Roussel, Vogel — CHI 2012) underpins our temporal landmark smoothing.
- Everyone who has tested an early build and reported what worked and what didn't.
