// effect_graph.dart
// =============================================================================
// Community AR — EffectGraph (Dart side)
//
// Immutable value type holding the list of effects to apply, in order.
// The EffectGraph is the Dart-side declarative description; it gets pushed
// to native via the method channel when the user calls
// `CommunityARView`'s `effects` property or `session.setEffects(graph)`.
//
// Value semantics:
//   - EffectGraph instances are immutable after construction.
//   - Equality compares the effect list deeply via byte-level serialization
//     equivalence — two graphs producing the same bytes ARE the same graph.
//     This makes Flutter's rebuild optimizations work naturally.
//
// Phase 2 only renders the first effect (since composition isn't supported
// yet on the native side until Phase 3). The list-of-effects shape is
// already in place so Phase 3 adoption is additive.
// =============================================================================

import 'dart:typed_data';
import 'package:flutter/foundation.dart';

import 'effect.dart';

@immutable
class EffectGraph {
  /// Empty graph — useful as a default and for clearing effects.
  static const EffectGraph empty = EffectGraph._(<Effect>[]);

  /// The list of effects, run in order. The output of effect i becomes
  /// the input of effect i+1.
  final List<Effect> effects;

  EffectGraph({List<Effect> effects = const <Effect>[]})
      : effects = List<Effect>.unmodifiable(effects);

  const EffectGraph._(this.effects);

  bool get isEmpty => effects.isEmpty;

  /// Equality is based on the serialized bytes of each effect. Two graphs
  /// that produce identical FFI payloads are considered equal.
  @override
  bool operator ==(Object other) {
    if (identical(this, other)) return true;
    if (other is! EffectGraph) return false;
    if (effects.length != other.effects.length) return false;
    for (int i = 0; i < effects.length; ++i) {
      if (effects[i].typeId != other.effects[i].typeId) return false;
      final a = effects[i].serialize();
      final b = other.effects[i].serialize();
      if (!_bytesEqual(a, b)) return false;
    }
    return true;
  }

  @override
  int get hashCode {
    int h = 0;
    for (final e in effects) {
      h = Object.hash(h, e.typeId);
      final bytes = e.serialize();
      // Cheap rolling hash over the bytes
      for (int b in bytes) {
        h = Object.hash(h, b);
      }
    }
    return h;
  }

  static bool _bytesEqual(Uint8List a, Uint8List b) {
    if (a.length != b.length) return false;
    for (int i = 0; i < a.length; ++i) {
      if (a[i] != b[i]) return false;
    }
    return true;
  }
}
