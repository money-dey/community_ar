// effect.dart
// =============================================================================
// Community AR — Effect base class (Dart side)
//
// Every user-facing effect is a subclass of Effect with two responsibilities:
//   1. Hold its user-facing config as immutable Dart fields
//   2. Serialize that config to bytes matching the C ABI struct layout
//
// The base class is sealed in spirit (we don't expect third-party subclasses
// in v1) but kept open in code so internal effects can extend it freely.
// =============================================================================

import 'dart:typed_data';

/// Base class for all Community AR effects.
///
/// Each concrete subclass corresponds to a numeric `typeId` known to the
/// native side. The `serialize()` method packs the effect's config into a
/// byte buffer whose layout matches the corresponding `CAR<...>Config`
/// C struct in `community_ar_phase2_api.h`.
abstract class Effect {
  const Effect();

  /// Stability-bound numeric ID matching the C ABI's CAR_EFFECT_TYPE_*.
  int get typeId;

  /// Serialize this effect's config to bytes for FFI transit.
  ///
  /// The returned bytes are copied across the C ABI; the Effect object
  /// can be discarded afterward.
  Uint8List serialize();
}
