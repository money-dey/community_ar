// lips_effect.dart
// =============================================================================
// Community AR — LipsEffect (Dart side, user-facing API)
//
// Usage:
//   CommunityARView(
//     camera: CameraLens.front,
//     effects: EffectGraph(effects: [
//       LipsEffect(color: Color(0xFFCC0033)),
//     ]),
//   )
//
// The `color` parameter is sRGB 8-bit per channel — same as Flutter's
// standard Color type. All perceptual color-space math (Oklab) happens
// natively on the GPU; Dart never sees Oklab values.
// =============================================================================

import 'dart:typed_data';
import 'package:flutter/painting.dart' show Color;

import 'effect.dart';

class LipsEffect extends Effect {
  /// Target lipstick color in sRGB. Alpha is ignored — use [opacity] instead.
  final Color color;

  /// Overall effect strength, [0, 1]. 0 = no effect, 1 = maximum saturation.
  /// The default 0.85 matches a natural, slightly-translucent lipstick look.
  final double opacity;

  /// Mask edge softness, [0, 1]. Higher = softer / more gradient at the lip
  /// outline. The default 0.4 hides the polygonal contour without looking
  /// blurry on closeup faces.
  final double edgeSoftness;

  /// How much of the source lip's brightness to preserve, [0, 1].
  ///   1.0 = keep all source luminance → natural lipstick (default)
  ///   0.0 = use target color's luminance → flat painted look
  /// Values between give intermediate "tinted balm" to "matte lipstick" looks.
  final double luminancePreserve;

  const LipsEffect({
    required this.color,
    this.opacity = 0.85,
    this.edgeSoftness = 0.4,
    this.luminancePreserve = 1.0,
  })  : assert(opacity >= 0 && opacity <= 1),
        assert(edgeSoftness >= 0 && edgeSoftness <= 1),
        assert(luminancePreserve >= 0 && luminancePreserve <= 1);

  @override
  int get typeId => 1; // CAR_EFFECT_TYPE_LIPS

  @override
  Uint8List serialize() {
    // Layout MUST match CARLipsEffectConfig in community_ar_phase2_api.h:
    //   uint32 version (= 1)
    //   float colorR, colorG, colorB
    //   float opacity, edgeSoftness, luminancePreserve
    // Total: 28 bytes, host-byte-order.
    final bytes = ByteData(28);
    bytes.setUint32(0, 1, Endian.host); // version
    bytes.setFloat32(4, color.red / 255.0, Endian.host);
    bytes.setFloat32(8, color.green / 255.0, Endian.host);
    bytes.setFloat32(12, color.blue / 255.0, Endian.host);
    bytes.setFloat32(16, opacity, Endian.host);
    bytes.setFloat32(20, edgeSoftness, Endian.host);
    bytes.setFloat32(24, luminancePreserve, Endian.host);
    return bytes.buffer.asUint8List();
  }

  LipsEffect copyWith({
    Color? color,
    double? opacity,
    double? edgeSoftness,
    double? luminancePreserve,
  }) {
    return LipsEffect(
      color: color ?? this.color,
      opacity: opacity ?? this.opacity,
      edgeSoftness: edgeSoftness ?? this.edgeSoftness,
      luminancePreserve: luminancePreserve ?? this.luminancePreserve,
    );
  }
}
