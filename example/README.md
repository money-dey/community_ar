# Community AR — example app

A runnable demo showing the library in action: front camera, beauty
preset picker, lipstick palette, live quality-tier indicator.

This `example/` directory has a dual purpose:

1. **For users evaluating the library** — run it on your own device, see
   what Community AR actually does on real faces in real lighting.
2. **For developers integrating the library** — read `lib/main.dart` as a
   reference implementation showing the canonical Flutter wiring.

## Running it

```sh
cd example
flutter run
```

You'll need a real device or an emulator with camera support. The
front-camera path is the most thoroughly demoed.

## What you'll see

- A live camera preview with beauty + lipstick composited
- Top bar toggles for enabling/disabling each effect
- Top-bar badge showing the auto-resolved beauty quality tier
- Bottom panel: nine beauty presets + an eight-color lipstick palette
  with opacity slider

The presets that demonstrate Community AR's cross-skin-tone correctness
most clearly are `glamour` (heavy smoothing) and `editorial` (heavy
mid-band attenuation). Try them on different skin tones to see the
tone-aware threshold scaling at work — no per-tone tuning is needed.

## About testing your integration

Community AR has its own comprehensive testing — see
[`../TESTING.md`](../TESTING.md) for the project's testing posture
(off-device unit tests, on-device golden image tests, performance
benchmarks, visual verification protocol across diverse skin tones).
You don't need to verify the library's internals; that's the maintainers'
job.

What *is* worth testing in your own app is your **integration** with
the library — the glue code that decides when to enable an effect,
which preset to pick, how to react to permission denials. A few focused
examples follow. Treat them as a starting point, not a checklist —
they're the kind of tests you'd write for any third-party widget.

### Example: testing that your UI state composes the right effect graph

```dart
// test/beauty_toggle_test.dart
import 'package:flutter_test/flutter_test.dart';
import 'package:community_ar/community_ar.dart';
import 'package:my_app/beauty_controller.dart';

void main() {
  test('disabled controller produces an empty effect graph', () {
    final controller = BeautyController(enabled: false);
    expect(controller.toEffectGraph().effects, isEmpty);
  });

  test('enabled controller composes SkinSmoothEffect with the chosen preset', () {
    final controller = BeautyController(
      enabled: true,
      preset: BeautyPresets.glamour,
    );
    final effects = controller.toEffectGraph().effects;
    expect(effects, hasLength(1));
    expect(effects.first, isA<SkinSmoothEffect>());
  });
}
```

### Example: testing config validation in your settings screen

```dart
test('rejects out-of-range smoothing strength before sending to library', () {
  expect(
    () => BeautyFilterConfig(smoothingStrength: 1.5).validate(),
    throwsArgumentError,
  );
});
```

Catching this in your settings screen is better than catching it as a
runtime error inside the render pipeline — your app can show a friendly
error message; the library can only refuse to install the effect.

### Example: widget test for camera-permission denial UI

```dart
testWidgets('shows fallback UI when camera permission is denied',
    (tester) async {
  // Your permission wrapper, not the library's
  await tester.pumpWidget(MyAppRoot(cameraPermissionGranted: false));
  expect(find.text('Camera access required'), findsOneWidget);
  expect(find.byType(CommunityARView), findsNothing);
});
```

That's it. Three tests for the integration boundary, not for the library
itself. The library is tested separately; your tests just need to cover
the wiring you wrote.

## File layout

```
example/
├── lib/
│   └── main.dart           # The demo — read this for canonical wiring
├── pubspec.yaml
└── README.md               # You're reading it
```
