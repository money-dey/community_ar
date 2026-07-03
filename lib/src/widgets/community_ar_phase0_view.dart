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
//   - app pause   → stop the camera (release it for other apps)
//   - app resume  → restart the camera (the EGL pipeline + session survive
//                   backgrounding; only Camera2 is torn down/reopened). Without
//                   this the preview stays frozen on the last frame after the
//                   OS disconnects the camera while backgrounded.
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

class _CommunityARPhase0ViewState extends State<CommunityARPhase0View>
    with WidgetsBindingObserver {
  int? _textureId;
  int _width = 0;
  int _height = 0;
  String? _error;

  // App-lifecycle state. `_sessionReady` gates lifecycle handling until the
  // initial createSession+startCamera has run; `_cameraPaused` tracks whether we
  // stopped the camera on background so resume restarts exactly once.
  bool _sessionReady = false;
  bool _cameraPaused = false;

  // Pinch-to-zoom state. `_minZoom`/`_maxZoom` are the active camera's range
  // (hardware range when supported — `_minZoom` can be < 1.0 on ultra-wide
  // devices — else 1.0..digital cap); `_zoom` is the live factor and
  // `_baseZoom` the factor captured at gesture start.
  double _minZoom = 1.0;
  double _maxZoom = 1.0;
  double _zoom = 1.0;
  double _baseZoom = 1.0;

  String get _lensName =>
      widget.camera == Phase0CameraLens.front ? 'front' : 'back';

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _init();
  }

  Future<void> _init() async {
    try {
      await CommunityARPhase0FFI.createSession();
      await CommunityARPhase0FFI.startCamera(lens: _lensName);
      await CommunityARPhase0FFI.setTestMode(widget.testMode);
      _sessionReady = true;
      _maxZoom = await CommunityARPhase0FFI.getMaxZoom();
      _minZoom = await CommunityARPhase0FFI.getMinZoom();

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
      CommunityARPhase0FFI.switchCamera(lens: _lensName);
      // New camera → reset zoom and refresh its range (backend/range differ).
      _zoom = 1.0;
      _refreshZoomRange();
    }
  }

  Future<void> _refreshZoomRange() async {
    final maxZ = await CommunityARPhase0FFI.getMaxZoom();
    final minZ = await CommunityARPhase0FFI.getMinZoom();
    if (mounted) {
      _maxZoom = maxZ;
      _minZoom = minZ;
    }
  }

  // --------------------------------------------------------------------------
  // App lifecycle: release the camera on background, restart it on resume so the
  // preview doesn't stay frozen on the last frame after the OS disconnects the
  // camera. The native session + GL/EGL pipeline persist across background, so
  // only Camera2 is stopped/reopened (the Flutter texture id is unchanged).
  // --------------------------------------------------------------------------
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (!_sessionReady) return;
    switch (state) {
      case AppLifecycleState.resumed:
        if (_cameraPaused) {
          _cameraPaused = false;
          _zoom = 1.0; // a fresh camera stream starts un-zoomed
          CommunityARPhase0FFI.startCamera(lens: _lensName);
        }
      case AppLifecycleState.inactive:
      case AppLifecycleState.paused:
      case AppLifecycleState.hidden:
      case AppLifecycleState.detached:
        if (!_cameraPaused) {
          _cameraPaused = true;
          CommunityARPhase0FFI.stopCamera();
        }
    }
  }

  void _onScaleStart(ScaleStartDetails _) => _baseZoom = _zoom;

  void _onScaleUpdate(ScaleUpdateDetails d) {
    // Only the pinch component matters; single-finger drags report scale ~1.0.
    // Clamp to the device range — `_minZoom` is < 1.0 on ultra-wide cameras.
    final z = (_baseZoom * d.scale).clamp(_minZoom, _maxZoom);
    if ((z - _zoom).abs() < 0.001) return;
    _zoom = z;
    // Fire-and-forget; native clamps and applies on the render/camera thread.
    CommunityARPhase0FFI.setZoom(z);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
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
    return GestureDetector(
      // Pinch to zoom. behavior=opaque so the whole preview area is hit-testable
      // even where the Texture doesn't paint (letterbox margins under BoxFit).
      behavior: HitTestBehavior.opaque,
      onScaleStart: _onScaleStart,
      onScaleUpdate: _onScaleUpdate,
      child: FittedBox(
        fit: widget.fit,
        child: SizedBox(
          width: _width.toDouble(),
          height: _height.toDouble(),
          child: Texture(textureId: _textureId!),
        ),
      ),
    );
  }
}
