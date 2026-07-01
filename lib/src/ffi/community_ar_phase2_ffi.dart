// community_ar_phase2_ffi.dart
// =============================================================================
// Phase 2 method-channel surface for effect graph management.
//
// The Dart-side EffectGraph is value-typed and immutable. To install it
// natively, we call `setEffectGraph(graph)`, which serializes each effect
// to bytes and pushes the whole list across the method channel.
//
// The native side (Phase0Session) receives this on the platform thread,
// validates the effect type IDs, and queues the swap onto the render
// thread so the next frame renders with the new graph.
// =============================================================================

import 'package:flutter/services.dart';
import '../effects/effect_graph.dart';

class CommunityARPhase2FFI {
  CommunityARPhase2FFI._();

  static const _channel = MethodChannel('dev.communityar/methods');

  /// Install (or replace) the effect graph. Returns once the native side
  /// has accepted the call; the actual GPU swap happens on the next frame.
  ///
  /// Throws [PlatformException] if any effect's type ID is unknown to the
  /// native side.
  static Future<void> setEffectGraph(EffectGraph graph) async {
    if (graph.isEmpty) {
      await _channel.invokeMethod<void>('clearEffectGraph');
      return;
    }

    // Build parallel arrays matching the C ABI:
    //   typeIds:  List<int>
    //   configs:  List<Uint8List>
    final typeIds = <int>[];
    final configs = <Uint8List>[];

    for (final effect in graph.effects) {
      typeIds.add(effect.typeId);
      configs.add(effect.serialize());
    }

    await _channel.invokeMethod<void>('setEffectGraph', {
      'typeIds': typeIds,
      'configs': configs,
    });
  }

  /// Returns the count of currently-installed effects (diagnostic).
  static Future<int> getEffectCount() async {
    final n = await _channel.invokeMethod<int>('getEffectCount');
    return n ?? 0;
  }
}
