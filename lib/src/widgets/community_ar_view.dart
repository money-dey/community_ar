// community_ar_view.dart
// =============================================================================
// Community AR — Phase 2 widget surface.
//
// Adds the `effects:` parameter to CommunityARView. When the supplied
// EffectGraph changes (by value comparison), we push the new graph to
// native via the method channel.
//
// Internally this wraps the Phase 0 CommunityARPhase0View — the camera
// pipeline and Flutter texture handoff are unchanged. The only Phase 2
// thing is the effects list being applied to whatever the camera shows.
// =============================================================================

import 'package:flutter/material.dart';

import '../effects/effect_graph.dart';
import '../ffi/community_ar_phase2_ffi.dart';
import 'community_ar_phase0_view.dart';

/// Camera selection. (Re-exported from the Phase 0 widget for convenience.)
enum CameraLens { front, back }

class CommunityARView extends StatefulWidget {
  /// Which physical camera to use.
  final CameraLens camera;

  /// The effect graph to apply. Pass `EffectGraph.empty` for plain camera
  /// passthrough. The widget compares graphs by value (deep effect compare)
  /// so identical graphs across rebuilds don't trigger a native swap.
  final EffectGraph effects;

  /// How to fit the camera output into the widget's bounds.
  final BoxFit fit;

  const CommunityARView({
    super.key,
    this.camera = CameraLens.front,
    this.effects = EffectGraph.empty,
    this.fit = BoxFit.cover,
  });

  @override
  State<CommunityARView> createState() => _CommunityARViewState();
}

class _CommunityARViewState extends State<CommunityARView> {
  EffectGraph? _lastApplied;

  @override
  void initState() {
    super.initState();
    _applyEffectsIfChanged();
  }

  @override
  void didUpdateWidget(covariant CommunityARView old) {
    super.didUpdateWidget(old);
    _applyEffectsIfChanged();
  }

  void _applyEffectsIfChanged() {
    final g = widget.effects;
    if (_lastApplied == g) return;
    _lastApplied = g;
    // Fire-and-forget; the native side queues the swap onto the render thread.
    //
    // Degrade gracefully if the platform hasn't wired the effect-graph channel
    // yet (e.g. a Phase 0-only build): a MissingPluginException here would
    // otherwise surface as an unhandled async exception on every rebuild.
    CommunityARPhase2FFI.setEffectGraph(g).catchError((Object _) {});
  }

  @override
  Widget build(BuildContext context) {
    // The actual texture pipeline lives in the Phase 0 widget. Phase 2 only
    // changes what shaders run on the frame before it reaches Flutter.
    return CommunityARPhase0View(
      camera: widget.camera == CameraLens.front
          ? Phase0CameraLens.front
          : Phase0CameraLens.back,
      fit: widget.fit,
    );
  }
}
