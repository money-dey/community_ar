// beauty_filter_config.dart
// =============================================================================
// BeautyFilterConfig — the user-facing configuration for skin beautification.
//
// 13 floats + a quality enum. Defaults produce a natural look that works
// across a wide range of faces; the [BeautyPresets] class provides
// ready-made starting points for common aesthetics.
//
// Field semantics (full descriptions in car-phase-3-requirements.md Decision 5):
//
//   smoothingStrength       — overall amount of skin smoothing
//   detailPreserve          — how much pore/texture detail to keep
//   blemishReduction        — strength of localized blemish reduction
//   bilateralEdgeSensitivity — how aggressively bilateral preserves edges
//   highFreqStrength        — pore-level high-frequency band weight
//   midFreqStrength         — wrinkle-attenuation mid-frequency band weight
//   warmth                  — color temperature push (positive = warmer)
//   highlightLift           — lift in highlight regions
//   clarity                 — local-contrast enhancement
//   specularControl         — matte (-1) ↔ neutral (0) ↔ glow (+1)
//   temporalSmoothing       — history blend weight for jitter suppression
//   adaptivenessLocal       — local exposure adaptation amount
//   quality                 — performance tier (auto recommended)
//
// Tone-awareness (the cross-skin-tone correctness) is automatic and not
// a user-facing knob. The shader uses the per-face skinTone.baselineLuma
// from PerceptionFrame to scale thresholds.
// =============================================================================

import 'dart:typed_data';

import 'effect.dart';
import 'beauty_quality.dart';

/// Configuration for [SkinSmoothEffect].
class BeautyFilterConfig {
  // ---- Core smoothing ----

  /// Overall amount of skin smoothing. Range: [0, 1]. Default: 0.7.
  final double smoothingStrength;

  /// How much pore/texture detail to preserve. Range: [0.5, 1]. Default: 0.8.
  /// Values below 0.5 produce visible "plastic skin" — never allowed.
  final double detailPreserve;

  /// Strength of localized blemish reduction. Range: [0, 1]. Default: 0.6.
  final double blemishReduction;

  /// Edge sensitivity of the bilateral filter. Range: [0, 1]. Default: 0.15.
  /// Higher = preserves more edges (less smoothing across them).
  final double bilateralEdgeSensitivity;

  // ---- Multi-band frequency separation ----

  /// Weight of the high-frequency band (pore-level detail).
  /// Range: [0, 1]. Default: 0.9.
  final double highFreqStrength;

  /// Weight of the mid-frequency band (wrinkle attenuation).
  /// Range: [0, 1]. Default: 0.5.
  final double midFreqStrength;

  // ---- Glow finishing ----

  /// Color temperature push (positive = warmer, negative = cooler).
  /// Range: [-0.2, 0.2]. Default: 0.04.
  final double warmth;

  /// Lift applied to highlight regions. Range: [0, 0.3]. Default: 0.08.
  final double highlightLift;

  /// Local contrast enhancement. Range: [0, 0.5]. Default: 0.15.
  final double clarity;

  // ---- Surface character ----

  /// Specular control. Range: [-1, 1]. Default: 0.0.
  ///   -1.0 → fully matte skin (suppress highlights)
  ///   0.0  → neutral (no change to specular response)
  ///   +1.0 → glowy skin (boost highlights, soft bloom)
  final double specularControl;

  // ---- Temporal + adaptive ----

  /// History-blend weight for temporal stabilization. Range: [0, 1]. Default: 0.7.
  /// Higher = more jitter suppression; reduced automatically on motion.
  final double temporalSmoothing;

  /// Local exposure adaptation strength. Range: [0, 1]. Default: 0.5.
  final double adaptivenessLocal;

  // ---- Quality tier ----

  /// Performance tier for the beauty pipeline. Default: [BeautyQuality.auto].
  final BeautyQuality quality;

  const BeautyFilterConfig({
    this.smoothingStrength = 0.7,
    this.detailPreserve = 0.8,
    this.blemishReduction = 0.6,
    this.bilateralEdgeSensitivity = 0.15,
    this.highFreqStrength = 0.9,
    this.midFreqStrength = 0.5,
    this.warmth = 0.04,
    this.highlightLift = 0.08,
    this.clarity = 0.15,
    this.specularControl = 0.0,
    this.temporalSmoothing = 0.7,
    this.adaptivenessLocal = 0.5,
    this.quality = BeautyQuality.auto,
  });

  /// Validates that all parameters are in their documented ranges. Throws
  /// [ArgumentError] if any are out of bounds.
  ///
  /// **Always call before passing the config across FFI** — the native
  /// side does NOT re-validate, on the assumption that Dart already has.
  /// Passing out-of-range values to the shader can produce visually
  /// surprising results (e.g. negative smoothing acts like sharpening).
  void validate() {
    _check('smoothingStrength', smoothingStrength, 0.0, 1.0);
    _check('detailPreserve', detailPreserve, 0.5, 1.0);
    _check('blemishReduction', blemishReduction, 0.0, 1.0);
    _check('bilateralEdgeSensitivity', bilateralEdgeSensitivity, 0.0, 1.0);
    _check('highFreqStrength', highFreqStrength, 0.0, 1.0);
    _check('midFreqStrength', midFreqStrength, 0.0, 1.0);
    _check('warmth', warmth, -0.2, 0.2);
    _check('highlightLift', highlightLift, 0.0, 0.3);
    _check('clarity', clarity, 0.0, 0.5);
    _check('specularControl', specularControl, -1.0, 1.0);
    _check('temporalSmoothing', temporalSmoothing, 0.0, 1.0);
    _check('adaptivenessLocal', adaptivenessLocal, 0.0, 1.0);
  }

  void _check(String name, double value, double min, double max) {
    if (value.isNaN || value < min || value > max) {
      throw ArgumentError.value(
          value, name, 'must be in [$min, $max] (got $value)');
    }
  }

  /// Returns a copy of this config with the given fields overridden.
  BeautyFilterConfig copyWith({
    double? smoothingStrength,
    double? detailPreserve,
    double? blemishReduction,
    double? bilateralEdgeSensitivity,
    double? highFreqStrength,
    double? midFreqStrength,
    double? warmth,
    double? highlightLift,
    double? clarity,
    double? specularControl,
    double? temporalSmoothing,
    double? adaptivenessLocal,
    BeautyQuality? quality,
  }) {
    return BeautyFilterConfig(
      smoothingStrength: smoothingStrength ?? this.smoothingStrength,
      detailPreserve: detailPreserve ?? this.detailPreserve,
      blemishReduction: blemishReduction ?? this.blemishReduction,
      bilateralEdgeSensitivity:
          bilateralEdgeSensitivity ?? this.bilateralEdgeSensitivity,
      highFreqStrength: highFreqStrength ?? this.highFreqStrength,
      midFreqStrength: midFreqStrength ?? this.midFreqStrength,
      warmth: warmth ?? this.warmth,
      highlightLift: highlightLift ?? this.highlightLift,
      clarity: clarity ?? this.clarity,
      specularControl: specularControl ?? this.specularControl,
      temporalSmoothing: temporalSmoothing ?? this.temporalSmoothing,
      adaptivenessLocal: adaptivenessLocal ?? this.adaptivenessLocal,
      quality: quality ?? this.quality,
    );
  }

  /// Serialize to bytes matching CARBeautyFilterConfig (v1) in
  /// native/core/ffi/community_ar_phase3_api.h.
  ///
  /// Layout: 60 bytes total (4 + 13*4 + 4 padding).
  Uint8List serialize() {
    final bytes = ByteData(60);
    bytes.setUint32(0, 1, Endian.host); // version
    bytes.setFloat32(4, smoothingStrength, Endian.host);
    bytes.setFloat32(8, detailPreserve, Endian.host);
    bytes.setFloat32(12, blemishReduction, Endian.host);
    bytes.setFloat32(16, bilateralEdgeSensitivity, Endian.host);
    bytes.setFloat32(20, highFreqStrength, Endian.host);
    bytes.setFloat32(24, midFreqStrength, Endian.host);
    bytes.setFloat32(28, warmth, Endian.host);
    bytes.setFloat32(32, highlightLift, Endian.host);
    bytes.setFloat32(36, clarity, Endian.host);
    bytes.setFloat32(40, specularControl, Endian.host);
    bytes.setFloat32(44, temporalSmoothing, Endian.host);
    bytes.setFloat32(48, adaptivenessLocal, Endian.host);
    bytes.setUint32(52, beautyQualityToCode(quality), Endian.host);
    bytes.setUint32(56, 0, Endian.host); // reserved padding
    return bytes.buffer.asUint8List();
  }
}
