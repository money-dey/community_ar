// skin_smooth_effect.dart
// =============================================================================
// SkinSmoothEffect — the user-facing Dart API for skin beautification.
//
// Usage:
//
//   CommunityARView(
//     camera: CameraLens.front,
//     effects: EffectGraph(effects: [
//       SkinSmoothEffect(config: BeautyPresets.natural),
//       LipsEffect(color: Color(0xFFCC0033)),
//     ]),
//   )
//
// The order in EffectGraph doesn't matter — SkinSmoothEffect has
// passOrder=SkinAdjust which sorts before LipsEffect's passOrder=Recolor.
// Lipstick gets applied to beautified skin automatically.
// =============================================================================

import 'dart:typed_data';

import 'effect.dart';
import 'beauty_filter_config.dart';

class SkinSmoothEffect extends Effect {
  /// The beauty configuration to apply.
  final BeautyFilterConfig config;

  /// Construct a SkinSmoothEffect with the given config.
  ///
  /// The config is validated immediately. If any parameter is out of
  /// range, [ArgumentError] is thrown — failures happen at construction
  /// time, not later at render time, so the stack trace points to your
  /// call site.
  SkinSmoothEffect({required this.config}) {
    config.validate();
  }

  /// Convenience constructor using a preset directly:
  ///
  ///   SkinSmoothEffect.preset(BeautyPresets.glamour)
  ///
  /// Equivalent to `SkinSmoothEffect(config: preset)`.
  SkinSmoothEffect.preset(BeautyFilterConfig preset) : config = preset {
    config.validate();
  }

  @override
  int get typeId => 8; // CAR_EFFECT_TYPE_SKIN_SMOOTH

  @override
  Uint8List serialize() => config.serialize();
}
