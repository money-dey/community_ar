// community_ar_phase0_view.dart
// =============================================================================
// Phase 0 Flutter widget.
//
// Drop into any Flutter app to verify the data highway works:
//
//   CommunityARPhase0View(
//     camera: Phase0CameraLens.front,
//     testMode: CARTestMode.passthrough,
//   )
//
// Lifecycle:
//   - initState   → createSession + startCamera
//   - dispose     → dispose
//   - app pause   → stop camera (NOT implemented in Phase 0; later phase)
// =============================================================================

import 'package:flutter/material.dart';
import '../ffi/community_ar_phase0_ffi.dart';

/// Phase 0's internal camera-selection enum. The public-facing
/// `CameraLens` lives in `community_ar_view.dart`; the Phase 2 widget
/// translates between them at the boundary.
enum Phase0CameraLens { front, back }

class CommunityARPhase0View extends StatefulWidget {
  final Phase0CameraLens camera;
  final CARTestMode testMode;
  final BoxFit fit;

  const CommunityARPhase0View({
    super.key,
    this.camera = Phase0CameraLens.front,
    this.testMode = CARTestMode.passthrough,
    this.fit = BoxFit.cover,
  });

  @override
  State<CommunityARPhase0View> createState() => _CommunityARPhase0ViewState();
}

class _CommunityARPhase0ViewState extends State<CommunityARPhase0View> {
  int? _textureId;
  int _width = 0;
  int _height = 0;
  String? _error;

  @override
  void initState() {
    super.initState();
    _init();
  }

  Future<void> _init() async {
    try {
      await CommunityARPhase0FFI.createSession();
      await CommunityARPhase0FFI.startCamera(
        lens: widget.camera == Phase0CameraLens.front ? 'front' : 'back',
      );
      await CommunityARPhase0FFI.setTestMode(widget.testMode);

      // Output dimensions are populated once the first frame has been
      // submitted; poll briefly for them.
      for (int i = 0; i < 30; i++) {
        await Future.delayed(const Duration(milliseconds: 33));
        final id = await CommunityARPhase0FFI.getOutputTextureId();
        final dims = await CommunityARPhase0FFI.getOutputDimensions();
        if (id >= 0 && dims.width > 0) {
          if (!mounted) return;
          setState(() {
            _textureId = id;
            _width = dims.width;
            _height = dims.height;
          });
          return;
        }
      }
      throw StateError('Native pipeline did not produce output texture');
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = e.toString());
    }
  }

  @override
  void didUpdateWidget(CommunityARPhase0View oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.testMode != widget.testMode) {
      CommunityARPhase0FFI.setTestMode(widget.testMode);
    }
    if (oldWidget.camera != widget.camera) {
      CommunityARPhase0FFI.switchCamera(
        lens: widget.camera == Phase0CameraLens.front ? 'front' : 'back',
      );
    }
  }

  @override
  void dispose() {
    CommunityARPhase0FFI.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_error != null) {
      return ColoredBox(
        color: Colors.black,
        child: Center(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Text(
              _error!,
              style: const TextStyle(color: Colors.red),
              textAlign: TextAlign.center,
            ),
          ),
        ),
      );
    }
    if (_textureId == null) {
      return const ColoredBox(
        color: Colors.black,
        child: Center(child: CircularProgressIndicator(color: Colors.white)),
      );
    }
    return FittedBox(
      fit: widget.fit,
      child: SizedBox(
        width: _width.toDouble(),
        height: _height.toDouble(),
        child: Texture(textureId: _textureId!),
      ),
    );
  }
}
