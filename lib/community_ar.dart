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
//       SkinSmoothEffect(config: BeautyPresets.natural),
//       LipsEffect(color: Colors.red),
//     ]),
//   )
//
// Effect ordering is automatic — beauty runs before recolor regardless of
// the order you list them in.
// =============================================================================

// Widget
export 'src/widgets/community_ar_view.dart' show CommunityARView, CameraLens;

// Effects — core
export 'src/effects/effect.dart' show Effect;
export 'src/effects/effect_graph.dart' show EffectGraph;

// Effects — Phase 2
export 'src/effects/lips_effect.dart' show LipsEffect;

// Effects — Phase 3 (beauty filter)
export 'src/effects/skin_smooth_effect.dart' show SkinSmoothEffect;
export 'src/effects/beauty_filter_config.dart' show BeautyFilterConfig;
export 'src/effects/beauty_quality.dart' show BeautyQuality;
export 'src/effects/beauty_preset.dart' show BeautyPresets;

// Diagnostics — Phase 1 perception stats
export 'src/ffi/community_ar_phase1_ffi.dart'
    show CARPerceptionStats, CARPerceptionRequest, CommunityARPhase1FFI,
         DebugOverlayMode;

// Diagnostics — Phase 3 beauty / mask pool
export 'src/ffi/community_ar_phase3_ffi.dart' show CommunityARPhase3FFI;
