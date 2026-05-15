# CommunityAR

**Open-source Snapchat-class face AR Flutter package.**

---

Most face AR libraries are either closed-source SDKs with restrictive terms or research-grade projects without production polish. CommunityAR aims for both: open under Apache 2.0, *and* engineered with the rigor that real apps need — zero-copy GPU pipelines, perceptually uniform color math, multi-face tracking with stable IDs, sub-2ms effect overhead.


## How it works

```dart
import 'package:community_ar/community_ar.dart';

CommunityARView(
  camera: CameraLens.front,
  effects: EffectGraph(effects: [
    LipsEffect(color: Color(0xFFCC0033)),
  ]),
)
```

See `example/` for a runnable demo with a palette picker and live parameter sliders.

## Getting started

### Try the example app

```bash
git clone https://github.com/money-dey/community_ar
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
  community_ar: ^0.2.0
```

Then the three-line snippet from the top of this README is a working app.

# Call for contributors

**Technical contributions are warmly welcomed !**
Delivering the project's roadmap and associated milestones depends on contributions from a multidisciplinary community of Engineers, Testers, and Designers.

Priority areas at this stage of the project: device testing, shader development, on-device ML optimization, documentation, and design.

We particularly welcome contributors with experience in C++, GLES/Metal shader development, Machine Learning, Graphics, or Design.

Prior Flutter experience is not required.

For onboarding details, please refer to `CONTRIBUTORS.md`.

## License

Apache 2.0 — see [LICENSE](LICENSE). Bundled MediaPipe models are also Apache 2.0; see [`licenses/MEDIAPIPE_LICENSE.txt`](licenses/MEDIAPIPE_LICENSE.txt) for attribution requirements.

## Acknowledgements

- Face perception models from [Google's MediaPipe](https://ai.google.dev/edge/mediapipe).
- Oklab perceptual color space by [Björn Ottosson](https://bottosson.github.io/posts/oklab/).
- The One-Euro filter (Casiez, Roussel, Vogel — CHI 2012) underpins our temporal landmark smoothing.
- Everyone who has tested an early build and reported what worked and what didn't.
