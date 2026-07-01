# Testing Community AR

A guide to testing this project. **Read this before submitting your first PR.**

This document is for contributors at every level. If you're making your first
contribution, you don't need to understand all of it — sections marked
**Required for first PR** are the parts that matter most. Maintainers
preparing a release will find the full checklist further down.

> **Status (Phase 3, May 2026).** Most of what's described below is the
> intended testing posture. The test infrastructure itself is largely
> future work — first contribution opportunities, marked as such throughout.
> What exists today is the document you're reading and the structure for
> tests to land in. Be honest with yourself about which tier you're working
> in; pretending we have coverage we don't is worse than admitting the gap.

---

## Table of contents

1. [Why testing matters here](#why-testing-matters-here)
2. [The two-track testing model](#the-two-track-testing-model)
3. [Quick start for first-time contributors](#quick-start-for-first-time-contributors)
4. [Track 1 — Off-device tests](#track-1--off-device-tests)
5. [Track 2 — On-device tests](#track-2--on-device-tests)
6. [Testing specific subsystems](#testing-specific-subsystems)
7. [CI configuration](#ci-configuration)
8. [What we don't yet test](#what-we-dont-yet-test)
9. [For maintainers — release verification](#for-maintainers--release-verification)
10. [Adding tests with your PR](#adding-tests-with-your-pr)

---

## Why testing matters here

Community AR is an unusual library to test. The output is real-time GPU
rendering on a wide variety of mobile devices. There's no single "correct"
pixel value — outputs vary with GPU driver, floating-point precision,
camera sensor, lighting, and skin tone. Pixel-exact assertions are useless.

That doesn't mean we can skip testing. It means we need to be deliberate
about what we test, how, and why.

Consider what can go wrong if we don't:

- **A shader change produces a regression on dark skin only.** The developer
  didn't notice because they tested on their own face, which is light.
  Without cross-skin-tone test coverage, the regression ships and the
  project's central promise of working well across skin tones quietly breaks.
- **A refactor of the effect graph silently changes the order in which
  effects run.** Lipstick now applies to un-beautified skin instead of
  beautified skin. Visually subtle, but it's exactly the kind of bug that
  erodes trust over time.
- **A struct field is reordered in the C ABI.** Dart serialization no
  longer matches the C struct layout. Beauty parameters arrive in the wrong
  slots. The pipeline produces nonsense, and it's not obvious which struct
  field is wrong from the visual result alone.

All three are caught by tests that take seconds to run, if those tests exist.

The argument for testing isn't about purity. It's that **shader code touches
the pixels on people's faces in real time.** If we ship a broken filter,
people see it. They form a judgment about the library. That judgment
propagates. Testing is what lets us ship changes confidently without
asking real users to be our regression suite.

**Tests are part of the contribution, not separate from it.** A PR that
adds a new shader pass without tests for the math, without a golden image
for visual reference, and without a verification note from the contributor
is incomplete. Reviewers will ask for the missing pieces; better to
include them upfront.

---

## The two-track testing model

Tests in this project fall into two tracks. Both are required. Neither
substitutes for the other.

### Track 1 — Off-device tests

Run on any developer machine, in CI, with no special hardware. Fast — the
entire off-device test suite should finish in under 90 seconds. Focused
on logic, math, data structures, and contracts.

What off-device tests catch:
- Logic errors in Dart code (config validation, preset definitions)
- Logic errors in C++ code that doesn't require a GPU (effect graph sorting,
  mask pool lifecycle, perception data routing)
- Binary layout mismatches between Dart serialization and C structs
- Shader source code that fails to compile (caught by SPIRV validators
  without requiring GPU execution)
- Math errors in shaders, verified against CPU reimplementations of the
  same math

What off-device tests cannot catch:
- Rendering bugs that depend on GPU driver behavior
- Performance regressions
- Visual quality issues (does it look right?)
- Cross-device variance
- Camera capture and frame routing

### Track 2 — On-device tests

Run on real Android or iOS hardware (physical devices preferred; emulators
acceptable for some categories). Slow — minutes to tens of minutes per
device. Focused on rendering correctness, performance, and visual quality.

What on-device tests catch:
- Shader rendering output (the actual visual result)
- Frame timing and performance budget compliance
- Camera frame routing and zero-copy paths
- GPU driver-specific behavior
- Memory pressure under sustained load
- Cross-device variance

What on-device tests cannot catch (alone):
- Logic bugs that off-device tests would catch faster
- Subtle differences invisible to the naked eye that automated diff would
  catch — though we're working on this with golden images

### Why both are required

It is tempting to skip off-device tests if you have a phone in your hand
and you can see the filter working. Don't.

- Off-device tests run in CI on every PR. They catch regressions in
  pull requests before a human reviewer ever has to look at the change.
  You will discover, painfully, that a refactor you thought was clean
  broke a behavior six commits ago — and the off-device tests would have
  caught it the moment you made the change.
- On-device tests, in contrast, prove that the thing actually works on
  real hardware. They are slow and somewhat unreliable, but they are the
  only way to validate the things off-device tests can't reach. The
  example app's integration tests, the visual verification protocol,
  performance benchmarks — these are the on-device track.

**The right discipline:** write off-device tests first because they're
faster to iterate on, then verify on-device before you call your PR ready.

---

## Quick start for first-time contributors

If you are making your first PR, here is the minimum you need.

### Step 1 — Run the existing tests

From the repo root:

```sh
# Off-device — Dart unit tests
flutter test

# Off-device — C++ unit tests (after the first time, run this incrementally)
cmake -B build -DCAR_BUILD_TESTS=ON
cmake --build build --target community_ar_tests
ctest --test-dir build --output-on-failure
```

Together these should take under 90 seconds. **They should all pass before
you make any changes.** If they don't pass on a clean checkout, that's a
bug in the testing infrastructure — open an issue rather than working
around it.

### Step 2 — Make your change and add tests

Every PR that changes behavior needs a test. If you're changing:

- **Dart code** → add or update tests in `test/`
- **C++ logic (graph, pool, perception)** → add tests in `test/native/`
- **A shader** → add a math property test in `test/native/shaders/`,
  AND capture a before/after image pair following the
  [visual verification protocol](#visual-verification-protocol)
- **C ABI structs or function signatures** → update the ABI compatibility
  tests in `test/abi/`
- **Effect ordering, mask pool naming, or any architectural contract** →
  add a contract test in `test/native/effects/`

### Step 3 — Run off-device tests again

Make sure your additions pass. Make sure you didn't break anything else.

### Step 4 — Verify on a real device

If your change touches anything that ends up in the render pipeline,
build and run the example app on a real Android or iOS device. **Look at
your face on the screen.** Move around. Try the different presets. Try
the different cameras (front and back).

This is the cheapest, fastest on-device test you can do, and it catches a
surprising number of bugs that off-device tests cannot.

For changes that touch shaders: also run on a device with a skin tone
different from your own, if you can. We document where to find diverse
test inputs in [Visual verification protocol](#visual-verification-protocol).

### Step 5 — Document your testing in the PR description

Your PR description should answer:

- What did you test off-device? (run output, or at minimum "ran
  `flutter test` and `ctest`, all pass")
- What did you test on-device? Which devices, which scenarios?
- What did you NOT test that you think might matter? (Honesty here is
  appreciated; it tells reviewers where to focus.)

A PR with a thorough testing section in the description merges faster
than one without. Reviewers can confirm what you've already validated and
focus on what's left.

---

## Track 1 — Off-device tests

The fast feedback loop. Designed to run on every save (with watchers
configured locally) and on every push (in CI).

### Where they live

```
test/
├── dart/                       # Dart unit tests
│   ├── effects/
│   │   ├── beauty_filter_config_test.dart
│   │   ├── beauty_preset_test.dart
│   │   ├── effect_graph_test.dart
│   │   ├── lips_effect_test.dart
│   │   └── skin_smooth_effect_test.dart
│   ├── ffi/
│   │   ├── method_channel_mocks_test.dart
│   │   └── serialization_test.dart
│   └── widgets/
│       └── community_ar_view_test.dart
├── native/                     # C++ unit tests
│   ├── effects/
│   │   ├── effect_graph_test.cpp
│   │   ├── mask_resource_pool_test.cpp
│   │   └── pass_order_test.cpp
│   ├── perception/
│   │   ├── segmenter_backend_factory_test.cpp
│   │   └── face_tracker_test.cpp
│   └── shaders/                # Shader math property tests
│       ├── oklab_test.cpp
│       ├── p1_skin_mask_test.cpp
│       ├── p3_bilateral_test.cpp
│       ├── p5_composition_test.cpp
│       └── ... (one per shader)
└── abi/                        # ABI compatibility tests
    ├── struct_layout_test.cpp
    └── struct_layout_test.dart
```

### Dart unit tests

Standard Flutter testing — `package:flutter_test` and `package:test`.

**Use this for:** Anything that's pure Dart logic. Config validation,
preset definitions, serialization round-trips, effect graph composition
in Dart, mask pool name conventions.

**Mocking pattern.** Per the Flutter team's recommendation, prefer mocking
your own API over mocking method channels. For Community AR specifically,
this means wrapping `CommunityARPhase1FFI` and `CommunityARPhase3FFI` calls
in test doubles rather than mocking the underlying `MethodChannel`. The
exception is when you're testing the FFI surface itself — then you do
want to mock the channel directly with `TestDefaultBinaryMessenger`.

**What good Dart tests look like for this project:**

- `BeautyFilterConfig` rejects out-of-range values at construction
- `BeautyPresets.matte` produces a config with `specularControl < 0`
- `EffectGraph` orders `SkinSmoothEffect` (passOrder 100) before
  `LipsEffect` (passOrder 200) regardless of declaration order
- `BeautyFilterConfig.serialize()` produces exactly 60 bytes
- The first 4 bytes of any beauty config serialization are version=1
  (little-endian uint32)

### C++ unit tests

Use [Google Test](https://github.com/google/googletest) (preferred) or
Catch2 if you're allergic. Both are header-only or trivially vendored;
the build system supports either.

**Use this for:** C++ logic that doesn't require a GPU. The effect graph,
the mask pool, the segmenter backend factory, the perception pipeline's
routing logic.

**Mocking the RenderContext.** Most C++ tests don't need a real GPU. We
provide a `MockRenderContext` (in `test/native/support/mock_render_context.h`)
that records the GPU operations a piece of code would issue without
actually executing them. You can then assert on the recorded operation
log:

```cpp
TEST(EffectGraphTest, RunsEffectsInPassOrder) {
    MockRenderContext ctx;
    EffectGraph graph(&ctx);

    auto lips = std::make_unique<LipsEffect>(/* ... */);
    auto skinSmooth = std::make_unique<SkinSmoothEffect>(/* ... */);

    // Declaration order is "lips first, then beauty"
    std::vector<std::unique_ptr<Effect>> effects;
    effects.push_back(std::move(lips));
    effects.push_back(std::move(skinSmooth));
    graph.setEffects(std::move(effects));

    PerceptionFrame frame = makeMockFrame();
    Framebuffer* out = ctx.createOutputFbo();
    graph.render(ctx.dummyTexture(), frame, out);

    // Beauty (passOrder=100) should run before Lipstick (passOrder=200)
    auto log = ctx.recordedDraws();
    EXPECT_EQ(log[0].effectType, CAR_EFFECT_SKIN_SMOOTH);
    EXPECT_EQ(log[1].effectType, CAR_EFFECT_LIPS);
}
```

**What good C++ tests look like for this project:**

- `EffectGraph` sorts by `passOrder()` stably (same-passOrder effects
  preserve declaration order)
- `EffectGraph::perceptionInputs()` correctly ORs effect requirements
- `MaskResourcePool::put(name, nullptr)` is equivalent to remove
- `SegmenterBackendFactory` falls back to hair segmenter when multiclass
  model file is missing
- `SkinSmoothEffect::resolveAutoTier()` returns `High` when no benchmark
  data exists yet

### C ABI binary layout tests

These exist to prevent the most obscure-but-disastrous class of bug:
struct field reordering that silently breaks Dart ↔ C interop.

**The contract.** Every C struct used across the FFI boundary has a
documented binary layout. The Dart side serializes with `ByteData` using
hardcoded offsets. If a C struct field is added, removed, or reordered
without updating the Dart serializer, the data routed across the boundary
becomes garbage. The visual result is often "beauty parameters do the
wrong thing" without any obvious crash or error.

**The tests.** Two parallel test files, one in C++ and one in Dart:

```cpp
// test/abi/struct_layout_test.cpp
TEST(CARBeautyFilterConfigLayout, FieldOffsetsAreStable) {
    EXPECT_EQ(sizeof(CARBeautyFilterConfig), 60);
    EXPECT_EQ(offsetof(CARBeautyFilterConfig, version),               0);
    EXPECT_EQ(offsetof(CARBeautyFilterConfig, smoothingStrength),     4);
    EXPECT_EQ(offsetof(CARBeautyFilterConfig, detailPreserve),        8);
    // ... one assertion per field, in declaration order
    EXPECT_EQ(offsetof(CARBeautyFilterConfig, qualityTier),          52);
    EXPECT_EQ(offsetof(CARBeautyFilterConfig, reserved),             56);
}
```

```dart
// test/abi/struct_layout_test.dart
test('BeautyFilterConfig serialization matches CARBeautyFilterConfig layout', () {
  final cfg = BeautyFilterConfig(/* ... */);
  final bytes = cfg.serialize();
  expect(bytes.length, 60);

  final bd = ByteData.view(bytes.buffer);
  expect(bd.getUint32(0, Endian.host),  1);                  // version
  expect(bd.getFloat32(4, Endian.host), 0.7, exclusive: ...); // smoothingStrength
  // ... one assertion per field
});
```

The Dart test verifies the serializer produces the right bytes at the
right offsets. The C++ test verifies the C struct has the layout the
serializer assumes. Together they make struct evolution safe: change
either side without the other, and one of these tests fails.

### Shader compilation tests

A shader that doesn't compile is a runtime error on whatever the user
gets when they try to use it. We can catch compilation errors at test
time without requiring a GPU.

**The approach.** Use `glslangValidator` (from the
[glslang project](https://github.com/KhronosGroup/glslang)) to validate
every shader source string. The validator parses and type-checks GLSL ES
3.00 source the same way a real driver would.

```cpp
// test/native/shaders/compilation_test.cpp
TEST(ShaderCompilation, AllBeautyShadersCompile) {
    EXPECT_TRUE(validateGLSL(kP1FS, ShaderStage::Fragment, "GLSL ES 3.00"));
    EXPECT_TRUE(validateGLSL(kP2FS, ShaderStage::Fragment, "GLSL ES 3.00"));
    EXPECT_TRUE(validateGLSL(makeP3FragmentShader().c_str(),
                              ShaderStage::Fragment, "GLSL ES 3.00"));
    // ... one assertion per shader, including the helper-concatenated ones
}
```

This catches:
- Syntax errors
- Uniform/varying type mismatches
- Use of features outside the declared profile (e.g., GLSL ES 3.10
  features in a 3.00 shader)
- Concatenation bugs (the Oklab helper is prepended via string
  substitution; a typo in the prepending logic produces an unparseable
  shader, which this test would catch)

It does NOT catch math errors — those require either property tests
(below) or on-device golden tests.

### Shader math property tests

The clever part of off-device shader testing. The math inside each
shader pass is well-defined; we can reimplement it on the CPU and
verify that the GPU implementation produces the same outputs for the
same inputs.

**The approach.** For each shader, provide a CPU reference implementation
in C++ that does exactly the same math. Test that the CPU reference
produces known-correct outputs for synthetic inputs.

```cpp
// test/native/shaders/oklab_test.cpp
TEST(OklabConversion, RoundTripPreservesColorWithinEpsilon) {
    // Test colors spanning the sRGB gamut
    std::vector<RGB> testColors = {
        {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {0.5f, 0.5f, 0.5f}, {0.1f, 0.2f, 0.3f},
        // Skin-tone samples spanning a wide range
        {0.95f, 0.85f, 0.75f},  // very light
        {0.65f, 0.50f, 0.40f},  // medium
        {0.30f, 0.20f, 0.15f},  // dark
    };
    for (const auto& rgb : testColors) {
        auto lab = srgbToOklabRef(rgb);
        auto back = oklabToSrgbRef(lab);
        EXPECT_NEAR(back.r, rgb.r, 1e-4);
        EXPECT_NEAR(back.g, rgb.g, 1e-4);
        EXPECT_NEAR(back.b, rgb.b, 1e-4);
    }
}

TEST(OklabReference, MatchesBjornOttossonValues) {
    // Reference values from https://bottosson.github.io/posts/oklab/
    // for known input colors
    auto lab = srgbToOklabRef({1.0f, 1.0f, 1.0f});  // white
    EXPECT_NEAR(lab.L, 1.0f, 1e-3);   // L=1 for white
    EXPECT_NEAR(lab.a, 0.0f, 1e-3);
    EXPECT_NEAR(lab.b, 0.0f, 1e-3);
}
```

**For each shader pass, test the mathematical invariants the algorithm
guarantees.** Examples:

- **P5 (composition)** — when `smoothingStrength=0`, the output should
  equal the input within float precision (identity property)
- **P5 (composition)** — when `mask=0` everywhere, the output equals
  the input regardless of strength
- **P3 (bilateral)** — output for an all-one-color input equals the
  input (constant signals pass through unchanged)
- **P3 (bilateral)** — pixels on opposite sides of a sharp edge don't
  contribute to each other (range weights drop to near-zero across
  the edge)
- **P6.5 (temporal)** — when `motionMagnitude > 0.05`, output equals
  current frame exactly (disocclusion guard)
- **P6.5 (temporal)** — when `temporalSmoothing=0`, output equals
  current frame regardless of motion

These properties are what we *claim* the shaders do. Tests turn the
claims into enforceable contracts.

---

## Track 2 — On-device tests

The expensive but irreplaceable part of validation. These tests run on
real hardware. They prove the things off-device tests cannot.

### Where they live

```
test_on_device/                 # NB: separate dir from off-device tests
├── android/                    # Android JUnit tests (run via gradlew)
│   └── src/androidTest/java/...
├── ios/                        # iOS XCTest tests (run via xcodebuild test)
│   └── ...
example/
├── integration_test/           # Flutter integration tests
│   ├── beauty_pipeline_test.dart
│   ├── effect_composition_test.dart
│   └── performance_test.dart
└── test_inputs/                # Reference images for golden tests
    ├── skin_tones/             # Diverse skin tone samples
    ├── hair_textures/          # Diverse hair texture samples
    └── lighting_conditions/    # Various lighting scenarios
test_on_device/golden/          # Blessed reference images for diff
    ├── shaders/                # Per-shader-pass golden images
    │   ├── p1_skin_mask/
    │   ├── p5_composition/
    │   └── ...
    └── presets/                # Per-preset full-pipeline golden images
        ├── natural/
        ├── glamour/
        └── ...
```

**Why a separate top-level directory.** On-device tests are organized
differently from off-device — they're per-platform, have different build
systems, and large reference images shouldn't be in the main `test/`
tree that off-device runs touch. Keeping them separate also makes it
easy to exclude them from `pub publish` via `.pubignore`.

### Native unit tests (Android / iOS)

Test code that touches native APIs that require the real platform. Things
like: camera access permissions, hardware texture binding, GPU driver
behavior.

**Android.** Android JUnit instrumented tests, run via
`./gradlew :community_ar:connectedAndroidTest`. They require an emulator
or connected device.

**iOS.** XCTest tests, run via
`xcodebuild test -workspace ... -scheme CommunityAR -destination 'platform=iOS Simulator,...'`.
They require Xcode and a simulator.

**Use these for:** Validating that the JNI / ObjC++ adapters route data
correctly between Java/Swift and C++. The synthesized-method-call pattern
from the Flutter team's plugin testing wiki applies here — pretend a
method call came from Dart, run it through the adapter, validate the
response.

### Flutter integration tests

End-to-end tests of the Dart and native code together, running in the
example app's context. The most important on-device tests for this
project.

**Use these for:** Verifying that the full pipeline assembles and runs.
Build a beauty graph, install it, point the camera at a test pattern
(not a face), confirm the render pipeline produces output. Confirm the
mask pool gets populated. Confirm the auto-tier benchmark resolves to a
tier within 12 frames.

```dart
// example/integration_test/beauty_pipeline_test.dart
testWidgets('Beauty pipeline installs and renders without crash',
    (tester) async {
  await tester.pumpWidget(MaterialApp(
    home: CommunityARView(
      camera: CameraLens.front,
      effects: EffectGraph(effects: [
        SkinSmoothEffect(config: BeautyPresets.natural),
      ]),
    ),
  ));
  // Let the pipeline run for at least the benchmark window
  await tester.pumpAndSettle(const Duration(seconds: 2));

  final quality = await CommunityARPhase3FFI.getBeautyEffectiveQuality();
  expect(quality, isNot(BeautyQuality.auto),
      reason: 'Auto-tier should have resolved within 2 seconds');
});
```

These tests can't validate visual quality (no camera frames during
testing in CI), but they catch the catastrophic failure modes: graphs
that don't install, FFI calls that throw, sessions that don't start.

### Shader output golden tests

The crown jewel of on-device validation, and also the highest-maintenance.

**The approach.** Render each shader pass to a small framebuffer (e.g.,
256×256) with a known synthetic input. Read back the framebuffer. Compare
against a blessed reference image stored in the repo using a perceptual
diff algorithm.

**Why perceptual diff, not pixel-exact.** Pixel-exact comparison generates
false positives from GPU driver variance, floating-point precision
differences, and anti-aliasing variance. Industry consensus, drawn from
many years of visual regression testing experience: use SSIM (Structural
Similarity Index) or perceptual hash (PHASH) with a small tolerance.

**Recommended thresholds for this project:**
- SSIM threshold: ≥ 0.95 (output and reference are at least 95% structurally similar)
- Pixel mismatch threshold: < 1.0% of pixels differ by more than 4/255 in any channel
- PHASH distance: < 0.05 for full-pipeline outputs

**Important honest disclaimer.** Golden tests verify *consistency*, not
*correctness*. They tell you "the output of this shader is the same as
the last time we approved it." They don't tell you "the output is
correct" — the reference image could itself encode a bug, and the test
would keep passing as the bug persists.

This means: **when you bless a new reference image, review it as carefully
as code.** Bad baselines hide bugs for months.

**Test inputs to use.** For shader golden tests, synthetic inputs work
well (deterministic, reproducible). Examples:
- A solid color frame at various luminance levels
- A frame with a sharp edge at known position
- A frame with a known noise pattern
- A frame that's a recorded camera capture of a real face (with consent
  and license documented; see `test_on_device/test_inputs/README.md`)

### Performance benchmarks

The auto-tier benchmark in `SkinSmoothEffect` depends on accurate frame
timing. We need separate test infrastructure that measures real frame
times under realistic load and reports whether the pipeline meets its
performance budget.

**The protocol.**
1. Install a beauty effect with `BeautyQuality.high`
2. Render 300 frames against a synthetic input
3. Record per-frame timing
4. Assert: 95th percentile frame time is under the tier's budget
   - High: < 22 ms
   - Medium: < 26 ms
   - Low: < 30 ms

**Where this should run.** On a small set of reference devices that
represent the project's performance targets. Suggested baseline:
- Pixel 6a (Snapdragon equivalent, mid-range Android)
- iPhone 12 (A14, mid-range iOS)
- An older device — e.g., Pixel 4a or iPhone X — as a Low-tier reference

Performance benchmark results should be recorded in commit messages
when shaders change. This creates a longitudinal record of pipeline
performance over time.

### Visual verification protocol

For changes that affect what users see, automated tests are not enough.
A human needs to look at the output and judge whether it looks right.

This is the most important on-device test for this project, and the
hardest to systematize.

**The protocol for visual verification:**

1. **Capture a baseline.** Before your change, run the example app on
   your reference device, with the affected preset/configuration, and
   take a screenshot or short video. Use the same lighting and pose for
   both captures.

2. **Apply your change. Capture again.** Same lighting, same pose, same
   preset.

3. **Compare side by side.** Look for:
   - Any change you didn't intend
   - Any change that looks wrong on the specific feature you touched
   - Any change that looks wrong on features you didn't touch
     (regression check)

4. **Cross-skin-tone check.** This is critical for this project. Your
   own face is one data point. The library promises to work across
   skin tones. If you don't have access to diverse skin tone samples,
   the `test_on_device/test_inputs/skin_tones/` directory contains
   licensed reference faces spanning Fitzpatrick types I-VI (light to
   dark). Run your change against at least three samples spanning the
   range. Compare against the same samples' baselines.

5. **Capture both for the PR.** Attach the before/after pairs to your
   PR description. Reviewers can then see what you saw without having
   to reproduce your test setup.

**What to look for in beauty filter output:**
- Skin smoothing should reduce visible pores without making skin look
  plastic
- Edges of features (lips, eyes, brows, nostrils) should remain crisp
- The skin mask should follow the actual face boundary, not include
  hair or background
- Color should not shift visibly on dark skin (a common failure mode
  of naive RGB beauty filters)
- Temporal stability: small head movements should not produce visible
  ghosting or "crawling skin"

**What to look for in lipstick (or other recolor effects):**
- Color is applied to lips only, not chin/cheek/inside-of-mouth
- Color reads correctly across skin tones (a red lipstick should look
  red, not muddy or washed out)
- Mask edges are soft, not visibly polygonal
- Mouth opens → lipstick doesn't paint teeth/tongue
- Mouth closes → lipstick covers the full lip area

---

## Testing specific subsystems

Drilling into how to test each major piece of the architecture.

### Testing perception (segmenter backends, face tracker)

The perception layer turns camera frames into structured data. Most
of it can be tested off-device by feeding synthetic inputs.

**Off-device:**
- `SegmenterBackendFactory` selection logic (which backend gets chosen
  for which config, including the fallback chain when model files are
  missing)
- `FaceTracker` Hungarian-IoU assignment (synthetic faces in synthetic
  positions across synthetic frames; verify face IDs stay stable)
- `OneEuroFilter` smoothing properties (high-frequency input → smoothed
  output; sudden change → adaptive response)

**On-device:**
- Each backend actually runs without crashing on a connected device
- Multiclass segmenter produces a non-zero face-skin mask on a face
- Hair segmenter produces a non-zero hair mask on a face
- Both produce reasonable masks on dark hair / locs / braids (visual
  verification required)

### Testing the effect graph

The graph is pure logic — no GPU required for the architectural
contracts.

**Off-device:**
- Pass-order sorting is stable (same-passOrder effects preserve
  declaration order)
- Pass-order sorting is correct (effects ascend in passOrder value)
- `perceptionInputs()` union ORs flags correctly across multiple effects
- `maskRequirementsUnion()` deduplicates consumes/produces names
- `setEffects()` is atomic (old effects fully destroyed before new
  effects exposed)
- `findFirstEffectOfType()` returns the first match in pass-order
- `MaskResourcePool` named keys match the documented constants
  (`kFaceSkin`, `kHair`, etc.)

**On-device:**
- A graph with `SkinSmoothEffect` + `LipsEffect` runs both passes per
  frame
- Removing the beauty effect from a running graph doesn't leak GPU
  resources

### Testing each shader pass

See [Shader math property tests](#shader-math-property-tests) for the
off-device pattern. For on-device golden tests, use the protocol in
[Shader output golden tests](#shader-output-golden-tests).

**Per-pass test priorities:**

- **P1 (skin mask refinement)** — High priority. Bugs here propagate
  to every downstream pass.
  - Off-device: combination of segmenter+landmark exclusion produces
    correct mask values for known inputs
  - On-device: refined mask follows face boundary on diverse skin tones

- **P2 (downsample)** — Medium priority. Math is straightforward;
  bugs here usually produce obviously wrong results.
  - Off-device: 13-tap weighted sum CPU reference matches GPU output
    for known input patterns
  - On-device: visual smoke test (does the downsampled image look like
    a downsampled version of the input?)

- **P3 (bilateral)** — High priority. The most algorithmically complex
  pass; many subtle ways to get the math wrong.
  - Off-device: edge preservation property (sharp edges in input stay
    sharp in output)
  - Off-device: constant signal property (uniform color input → uniform
    color output)
  - On-device: cross-skin-tone results (mid-band and low-band masks
    look right on light, medium, dark skin)

- **P4 (upsample)** — Low priority. Simple math.
  - Off-device: 4-tap tent CPU reference matches GPU
  - On-device: smoke test only

- **P5 (multi-band composition)** — Highest priority. The heart of the
  algorithm; most contributor changes will touch this.
  - Off-device: identity property at `smoothingStrength=0`
  - Off-device: mask gating (`mask=0` → output equals input)
  - Off-device: band decomposition CPU reference matches GPU
  - On-device: golden images for each preset across diverse skin tones

- **P5.5 (specular control)** — Medium priority.
  - Off-device: zero-control passthrough; matte direction reduces
    specular L; glow direction increases it
  - On-device: golden for matte (-1), neutral (0), glow (+1)

- **P6 (glow finishing)** — Medium priority. Each operation
  (warmth/lift/clarity) is independent and testable.
  - Off-device: each operation has its own property test
  - On-device: golden for combined output

- **P6.5 (temporal stabilization)** — Medium-high priority. Subtle
  bugs here cause "crawling skin" that's hard to debug after the fact.
  - Off-device: blend math properties (smoothing=0 passthrough,
    high motion bypass)
  - On-device: stationary scene shows reduced jitter vs no temporal;
    moving scene shows no ghosting

### Testing the FFI surface

The C ABI is documented to be stable for a v1 release. Tests enforce
this.

**Off-device:**
- ABI binary layout tests (covered above)
- Versioned struct evolution: a `version=1` struct passed to a
  `version=2`-aware C parser is rejected, not silently misinterpreted
- Method channel mock tests for each Phase 3 FFI function

**On-device:**
- Round-trip integration tests: Dart serializes → native deserializes →
  effect runs → diagnostic queries return expected values

### Testing Flutter widgets

`CommunityARView` is mostly a thin wrapper. The main concerns are
lifecycle and prop-change handling.

**Off-device:**
- Widget rebuilds when `effects` prop changes
- Widget calls `disposeSession()` on unmount
- Widget calls `pauseSession()` when the app backgrounds
  (`WidgetsBindingObserver`)

**On-device:**
- Hot reload doesn't leak the previous session
- Switching camera lens cleanly tears down the old session

---

## CI configuration

What runs automatically in GitHub Actions, what doesn't, and why.

### What CI runs on every PR

- **Off-device track in full.** `flutter test`, `cmake --build`,
  `ctest`. Total time target: under 4 minutes including build.
- **Lint checks.** `dart analyze`, `clang-tidy` on the C++ codebase.
- **Shader compilation tests** via `glslangValidator`.
- **ABI compatibility tests** in both directions.

### What CI does NOT run

- **On-device tests of any kind.** GitHub Actions doesn't have GPU
  hardware on its standard runners. Running on Android emulator or
  iOS simulator is technically possible but slow and unreliable
  enough that we've chosen to make on-device testing a contributor
  and maintainer responsibility, not CI's.

  This is the most important thing to remember about this project's
  CI. **A green PR doesn't mean it works on a phone.** It means the
  off-device tests pass. You still need to test on-device before
  approving a merge.

- **Visual regression tests.** Golden image comparisons require
  rendering on real GPUs, which CI doesn't have. These run as part of
  the on-device track.

- **Performance benchmarks.** No reliable timing in CI virtual machines.

### How to interpret CI failures

- **Dart test failures** → look at the test output; usually
  straightforward
- **C++ test failures** → check the gtest output; segfaults will print
  a stack trace
- **ABI layout test failures** → you (or someone before you) changed a
  C struct or Dart serialization without updating the other side.
  Fix both before merging.
- **Shader compilation failures** → typo or invalid GLSL ES 3.00 in
  one of the shader source strings. The validator usually points at
  the specific line.
- **Lint failures** → fix the warnings; we keep these zero so
  reviewers can focus on substance

---

## What we don't yet test

Honest accounting of coverage gaps as of Phase 3. Each is an
opportunity for a first contribution.

- **Cross-device GPU driver variance.** We don't yet run shaders on a
  matrix of real devices to catch driver-specific bugs (Mali vs Adreno
  vs Apple GPU). This is the most impactful gap and is hard to close
  without a hardware farm.

- **Memory pressure tests.** No tests for behavior when the device is
  under memory pressure (other apps competing for GPU memory, low-RAM
  modes, etc.).

- **Long-running session tests.** No tests for sessions running >5
  minutes. Texture lifecycle bugs may exist that we won't catch in
  short tests.

- **Diverse skin tone golden images for every preset.** The
  `test_inputs/skin_tones/` directory has reference faces, but we
  don't yet have golden images blessed for every preset across every
  skin tone. This is a substantial first contribution: take the
  reference faces, run each preset, bless the output, document the
  process.

- **Hair texture golden images.** Same gap, for hair effects we
  haven't shipped yet (Phase 6+).

- **iOS-specific testing.** The repo's CI currently focuses on the
  Android path. iOS XCTest infrastructure is a future contribution.

- **Coverage measurement.** No automated tracking of how much of the
  codebase is exercised by tests. Adding `gcov` for C++ and `coverage`
  for Dart would tell us where the coverage gaps are concretely.

- **Property-based testing (Hypothesis / PropCheck style).** We use
  example-based tests today. Property-based testing — generating random
  inputs in known ranges and verifying invariants — would catch a class
  of bugs example tests miss. Particularly valuable for shader math.

If any of these gaps is interesting to you, we'd love a PR. Open an
issue first to discuss approach.

---

## For maintainers — release verification

Before tagging a release, the maintainer (or a designated reviewer)
runs through this checklist on at least one device per platform.

### Off-device verification

- [ ] All off-device tests pass on a clean checkout
- [ ] Lint and analysis pass with zero warnings
- [ ] ABI compatibility tests pass (struct layouts match Dart and C
      assumptions)
- [ ] CHANGELOG.md is up to date

### On-device verification — Android

- [ ] Example app builds with no warnings on Android Studio
- [ ] App launches successfully on the reference device
- [ ] All beauty presets produce visually correct output (per
      [Visual verification protocol](#visual-verification-protocol))
- [ ] Switching presets is smooth (no visible glitches)
- [ ] Front and back camera both work
- [ ] Auto quality tier resolves within ~2 seconds
- [ ] Performance benchmark passes on reference device:
      - High tier: 95th percentile < 22 ms
      - Medium tier: 95th percentile < 26 ms
      - Low tier: 95th percentile < 30 ms
- [ ] Diverse skin tone check: pipeline produces consistent visual
      quality across reference faces

### On-device verification — iOS

(Same checklist, on iOS reference device. Currently aspirational —
iOS CI infrastructure is a future contribution.)

### Cross-skin-tone verification

A specific call-out, given the project's positioning. Maintainers MUST
visually verify the pipeline against at least three faces spanning
light, medium, and dark skin before tagging a release. If results
visibly degrade on dark skin, that's a release blocker. Document the
verification in the release notes.

### Tag and release

- [ ] Version bump in `pubspec.yaml`
- [ ] Tag the commit (`git tag v0.x.y`)
- [ ] Push tag to origin
- [ ] (If publishing to pub.dev) `flutter pub publish`
- [ ] Update README status table if any Phase status changes

---

## Adding tests with your PR

Quick reference for what's required when you submit a PR.

### Test additions required

If your PR touches:

| Code area | Required tests |
|---|---|
| Dart API surface (new effect, new field) | Dart unit test |
| Effect graph behavior | Effect graph unit test |
| Mask pool naming or lifecycle | Mask pool unit test |
| C struct in the FFI surface | ABI compat test (both sides) |
| Shader math | Shader compilation test + property test + golden update |
| New shader pass | All of the above + performance baseline |
| Perception (segmenter, tracker, filter) | Perception unit test |
| Native code in adapter (JNI / ObjC++) | Native unit test |

### Test additions encouraged but optional

- Performance benchmark numbers for shader changes
- Golden images for visual changes
- Cross-device verification notes

### What goes in the PR description

A "Testing" section is required. Template:

```markdown
## Testing

**Off-device:** `flutter test` and `ctest` both pass on this branch.
Added tests for [what specifically].

**On-device:** Verified on [device model], [OS version].
- [ ] Beauty pipeline still produces correct output
- [ ] Lipstick still produces correct output
- [ ] Auto quality tier still resolves correctly
- [ ] No new visible regressions on [skin tones I checked]

**Not tested (intentionally):**
- [list anything you didn't test that a reviewer might wonder about]
```

Reviewers will use this section to understand what's been validated.
The more complete it is, the faster the review.

---

## Summary

Two tracks, both required:

- **Off-device** (CI, fast, runs everywhere): Dart unit tests, C++
  unit tests with MockRenderContext, ABI compat tests, shader
  compilation tests, shader math property tests.
- **On-device** (hardware required, slower, run by contributors and
  maintainers): native unit tests, Flutter integration tests, shader
  golden tests, performance benchmarks, visual / perceptual
  verification.

The cardinal sin in this project is shipping a change with no test —
not because the maintainer is going to reject your PR over it (we'll
usually just ask you to add one) but because untested changes corrode
the project's reliability over time. Every change touches real pixels
on real faces. Earning trust requires showing your work.

When in doubt, ask in your PR description. We'd rather discuss a test
gap than discover it later.
