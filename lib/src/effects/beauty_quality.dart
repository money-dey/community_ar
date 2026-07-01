// beauty_quality.dart
// =============================================================================
// BeautyQuality — runtime tier selection for the beauty filter.
//
// The library can adapt to device capability automatically (auto) or
// the user can force a tier. See car-phase-3-requirements.md Decision 7
// for the cost/quality trade-offs.
// =============================================================================

/// Quality tier for the skin beautification pipeline.
enum BeautyQuality {
  /// Pick a tier automatically based on a startup benchmark.
  ///
  /// On first activation, the pipeline runs at [high] for ~10 frames
  /// while measuring frame times. From frame 11 onward, it locks to the
  /// tier that fits the device's performance envelope.
  ///
  /// Recommended default for end-user apps; most users will land on
  /// [medium] on mid-range Android, [high] on flagships, [low] on
  /// older or thermally-throttled devices.
  auto,

  /// Full 9-pass pipeline. ~7ms on Snapdragon 7-class Android.
  /// Best quality; appropriate for flagship devices.
  high,

  /// Skip the low-frequency bilateral band (P3c/P3d).
  /// ~5ms on Snapdragon 7-class. Good quality at lower cost.
  medium,

  /// Skip low-frequency band AND specular control (P5.5);
  /// bilateral runs at quarter-resolution.
  /// ~3ms on Snapdragon 7-class. Acceptable quality on low-end devices.
  low,
}

/// Integer codes matching the CAR_BEAUTY_QUALITY_* constants in
/// community_ar_phase3_api.h. Used by [BeautyFilterConfig.serialize].
int beautyQualityToCode(BeautyQuality q) {
  switch (q) {
    case BeautyQuality.auto:
      return 0;
    case BeautyQuality.high:
      return 1;
    case BeautyQuality.medium:
      return 2;
    case BeautyQuality.low:
      return 3;
  }
}
