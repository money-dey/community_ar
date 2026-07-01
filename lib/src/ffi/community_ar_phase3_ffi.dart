// community_ar_phase3_ffi.dart
// =============================================================================
// Phase 3 method-channel surface — diagnostic accessors.
//
// The graph mutation API (setEffectGraph / clearEffectGraph) is unchanged
// from Phase 2; the SkinSmoothEffect is added to graphs via the same
// mechanism as LipsEffect. This file exposes new *diagnostic* surfaces
// that didn't exist in Phase 2:
//
//   - getBeautyEffectiveQuality() — useful when the user requested
//     BeautyQuality.auto and you want to display the resolved tier
//   - getMaskPoolNames()           — for debug overlays showing what
//     masks are active this frame
// =============================================================================

import 'package:flutter/services.dart';

import '../effects/beauty_quality.dart';

class CommunityARPhase3FFI {
  CommunityARPhase3FFI._();

  static const _channel = MethodChannel('dev.communityar/methods');

  /// Returns the currently-effective [BeautyQuality] tier for the installed
  /// beauty effect (if any). If the user requested [BeautyQuality.auto],
  /// this returns the resolved tier after the startup benchmark.
  ///
  /// Returns [BeautyQuality.auto] if no beauty effect is installed or
  /// if resolution hasn't completed yet (typically only true for the
  /// first ~10 frames after activation).
  static Future<BeautyQuality> getBeautyEffectiveQuality() async {
    final code = await _channel.invokeMethod<int>('getBeautyEffectiveQuality');
    switch (code) {
      case 1:
        return BeautyQuality.high;
      case 2:
        return BeautyQuality.medium;
      case 3:
        return BeautyQuality.low;
      case 0:
      default:
        return BeautyQuality.auto;
    }
  }

  /// Returns the list of mask names currently in the pool. Useful for
  /// debug overlays. Order is undefined; treat as a set.
  ///
  /// Common names you might see:
  ///   masks.faceSkin, masks.bodySkin, masks.hair, masks.clothes,
  ///   masks.background, masks.lipsContour, masks.refinedFaceSkin
  ///
  /// Returns an empty list if no graph is installed or no masks are
  /// active this frame.
  static Future<List<String>> getMaskPoolNames() async {
    final names = await _channel.invokeListMethod<String>('getMaskPoolNames');
    return names ?? const <String>[];
  }
}
