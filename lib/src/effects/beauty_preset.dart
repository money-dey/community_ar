// beauty_preset.dart
// =============================================================================
// BeautyPresets — pre-tuned starting configurations.
//
// Each preset is a complete [BeautyFilterConfig] tuned for a specific
// aesthetic. Users can use them directly or as a base for customization
// via [BeautyFilterConfig.copyWith].
//
// Design philosophy:
//   - "natural" is the recommended default (matches BeautyFilterConfig defaults)
//   - Each preset is opinionated: it doesn't try to please everyone, it
//     commits to one look
//   - Names describe what the look IS, not who it's for. "matte" is for
//     anyone who wants matte skin, "editorial" is for anyone who wants
//     editorial-style softness
// =============================================================================

import 'beauty_filter_config.dart';
import 'beauty_quality.dart';

class BeautyPresets {
  BeautyPresets._();  // not constructible

  /// No effect. Useful as an off-state without removing the effect from
  /// the graph (cheaper than rebuilding the graph just to disable beauty).
  static const off = BeautyFilterConfig(
    smoothingStrength: 0.0,
    blemishReduction: 0.0,
    warmth: 0.0,
    highlightLift: 0.0,
    clarity: 0.0,
    specularControl: 0.0,
    temporalSmoothing: 0.0,
  );

  /// Natural everyday look. Subtle smoothing, mild warmth, preserves
  /// most skin texture. Matches BeautyFilterConfig's defaults.
  static const natural = BeautyFilterConfig();

  /// Subtle — less aggressive than natural. Good for users who want
  /// "barely any filter" but still some polish.
  static const subtle = BeautyFilterConfig(
    smoothingStrength: 0.4,
    blemishReduction: 0.3,
    midFreqStrength: 0.3,
    warmth: 0.02,
    highlightLift: 0.04,
    clarity: 0.1,
  );

  /// Glamour — heavier smoothing, more highlight lift, polished look.
  /// "Going out" energy.
  static const glamour = BeautyFilterConfig(
    smoothingStrength: 0.9,
    blemishReduction: 0.8,
    midFreqStrength: 0.7,
    warmth: 0.06,
    highlightLift: 0.18,
    clarity: 0.25,
  );

  /// Soft glow — natural smoothing plus deliberate glow on the skin.
  static const softGlow = BeautyFilterConfig(
    specularControl: 0.5,
    highlightLift: 0.15,
    warmth: 0.06,
  );

  /// Matte — natural smoothing plus suppressed highlights. Good for
  /// "no-makeup makeup" looks and for reducing camera-shine on the T-zone.
  static const matte = BeautyFilterConfig(
    specularControl: -0.6,
    highlightLift: 0.0,
  );

  /// Editorial — heavy wrinkle attenuation but high detail preservation.
  /// Mimics a magazine-shoot retouching aesthetic.
  static const editorial = BeautyFilterConfig(
    smoothingStrength: 0.6,
    detailPreserve: 0.95,
    midFreqStrength: 0.2,
    highFreqStrength: 0.95,
    clarity: 0.35,
  );

  /// Low-light — boosts highlight lift to make dimly-lit faces readable.
  /// Useful for indoor / evening lighting.
  static const lowLight = BeautyFilterConfig(
    highlightLift: 0.22,
    adaptivenessLocal: 0.7,
    warmth: 0.06,
  );

  /// Studio — high clarity, neutral specular, balanced look. Mimics
  /// a controlled-lighting studio environment.
  static const studio = BeautyFilterConfig(
    clarity: 0.4,
    warmth: 0.0,
    highlightLift: 0.1,
    quality: BeautyQuality.high, // assume the user has performance budget
  );

  /// Ordered list of all presets — convenient for building UI pickers.
  static const List<MapEntry<String, BeautyFilterConfig>> all = [
    MapEntry('off',       off),
    MapEntry('natural',   natural),
    MapEntry('subtle',    subtle),
    MapEntry('glamour',   glamour),
    MapEntry('softGlow',  softGlow),
    MapEntry('matte',     matte),
    MapEntry('editorial', editorial),
    MapEntry('lowLight',  lowLight),
    MapEntry('studio',    studio),
  ];
}
