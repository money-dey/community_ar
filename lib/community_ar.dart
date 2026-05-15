// community_ar.dart
// =============================================================================
// Community AR — public Dart library surface.
//
// Importing this single file gives you everything needed to use the
// library in a Flutter app:
//
//   import 'package:community_ar/community_ar.dart';
//
//   CommunityARView(
//     camera: CameraLens.front,
//     effects: EffectGraph(effects: [
//       LipsEffect(color: Colors.red),
//     ]),
//   )
// =============================================================================

// Widget
export 'src/widgets/community_ar_view.dart' show CommunityARView, CameraLens;

// Effects
export 'src/effects/effect.dart' show Effect;
export 'src/effects/effect_graph.dart' show EffectGraph;
export 'src/effects/lips_effect.dart' show LipsEffect;

// Diagnostics (Phase 1 perception stats — useful during development)
export 'src/ffi/community_ar_phase1_ffi.dart'
    show CARPerceptionStats, CARPerceptionRequest, CommunityARPhase1FFI,
         DebugOverlayMode;
