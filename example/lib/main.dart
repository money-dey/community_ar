// main.dart — Phase 1 perception verification app
//
// Shows the camera feed with toggleable debug overlays. The point of this
// app is verification: when you can see landmark dots tracking your face,
// the hair mask tinting your hair, etc., perception is working.

import 'dart:async';
import 'package:flutter/material.dart';
import 'package:community_ar/community_ar.dart';

void main() => runApp(const Phase1App());

class Phase1App extends StatelessWidget {
  const Phase1App({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'Community AR Phase 1',
        theme: ThemeData.dark(),
        home: const Phase1Home(),
      );
}

class Phase1Home extends StatefulWidget {
  const Phase1Home({super.key});
  @override
  State<Phase1Home> createState() => _Phase1HomeState();
}

class _Phase1HomeState extends State<Phase1Home> {
  CameraLens _camera = CameraLens.front;

  // Debug overlay toggles
  bool _showLandmarks = true;
  bool _showIris      = false;
  bool _showHairMask  = false;
  bool _showPose      = false;

  // One-Euro filter sliders
  double _minCutoff = 1.0;
  double _beta      = 0.007;
  double _dCutoff   = 1.0;

  CARPerceptionStats _stats = const CARPerceptionStats(
    facesDetected: 0, faceMeshInferenceMs: 0, irisInferenceMs: 0,
    hairSegInferenceMs: 0, pnpSolveMs: 0, activeFilterCount: 0,
    skinBaselineLuma: 0, skinToneValid: false,
  );
  Timer? _statsTimer;

  @override
  void initState() {
    super.initState();

    // Force-enable the perception modules we want to visualize
    CommunityARPhase1FFI.forcePerception(const CARPerceptionRequest(
      needFaceLandmarks: true,
      needIris: true,
      needHair: true,
      needPose: true,
      needSkinTone: true,
    ));

    _applyOverlay();
    _statsTimer = Timer.periodic(const Duration(milliseconds: 500), (_) async {
      final s = await CommunityARPhase1FFI.getPerceptionStats();
      if (mounted) setState(() => _stats = s);
    });
  }

  void _applyOverlay() {
    int mask = DebugOverlayMode.none;
    if (_showLandmarks) mask |= DebugOverlayMode.landmarks;
    if (_showIris)      mask |= DebugOverlayMode.iris;
    if (_showHairMask)  mask |= DebugOverlayMode.hairMask;
    if (_showPose)      mask |= DebugOverlayMode.pose;
    CommunityARPhase1FFI.setDebugOverlay(mask);
  }

  void _applyFilter() {
    CommunityARPhase1FFI.setOneEuroParams(
      minCutoff: _minCutoff, beta: _beta, dCutoff: _dCutoff);
  }

  @override
  void dispose() {
    _statsTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Stack(
        children: [
          Positioned.fill(
            child: CommunityARPhase0View(camera: _camera, fit: BoxFit.cover),
          ),
          // Top: stats panel
          Positioned(
            top: 48, left: 16, right: 16,
            child: Container(
              padding: const EdgeInsets.all(8),
              color: Colors.black54,
              child: DefaultTextStyle(
                style: const TextStyle(color: Colors.white, fontSize: 12,
                                       fontFamily: 'monospace'),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('Faces: ${_stats.facesDetected}'),
                    Text('FaceMesh: ${_stats.faceMeshInferenceMs.toStringAsFixed(1)}ms'),
                    Text('Iris:     ${_stats.irisInferenceMs.toStringAsFixed(1)}ms'),
                    Text('HairSeg:  ${_stats.hairSegInferenceMs.toStringAsFixed(1)}ms'),
                    Text('PnP:      ${_stats.pnpSolveMs.toStringAsFixed(2)}ms'),
                    Text('Filters:  ${_stats.activeFilterCount}'),
                    Text('Skin luma: ${_stats.skinToneValid ? _stats.skinBaselineLuma.toStringAsFixed(3) : "—"}'),
                  ],
                ),
              ),
            ),
          ),
          // Bottom: controls
          Positioned(
            left: 0, right: 0, bottom: 0,
            child: Container(
              padding: const EdgeInsets.all(12),
              color: Colors.black54,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Wrap(spacing: 8, children: [
                    FilterChip(label: const Text('Landmarks'),
                      selected: _showLandmarks,
                      onSelected: (v) => setState(() { _showLandmarks = v; _applyOverlay(); })),
                    FilterChip(label: const Text('Iris'),
                      selected: _showIris,
                      onSelected: (v) => setState(() { _showIris = v; _applyOverlay(); })),
                    FilterChip(label: const Text('Hair'),
                      selected: _showHairMask,
                      onSelected: (v) => setState(() { _showHairMask = v; _applyOverlay(); })),
                    FilterChip(label: const Text('Pose'),
                      selected: _showPose,
                      onSelected: (v) => setState(() { _showPose = v; _applyOverlay(); })),
                  ]),
                  _slider('minCutoff', _minCutoff, 0.1, 10.0, (v) {
                    setState(() => _minCutoff = v); _applyFilter();
                  }),
                  _slider('beta', _beta, 0.0, 0.05, (v) {
                    setState(() => _beta = v); _applyFilter();
                  }),
                  ElevatedButton(
                    onPressed: () => setState(() {
                      _camera = _camera == CameraLens.front
                          ? CameraLens.back : CameraLens.front;
                    }),
                    child: Text(_camera == CameraLens.front
                        ? 'Use back camera' : 'Use front camera'),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _slider(String label, double value, double min, double max,
                 ValueChanged<double> onChanged) {
    return Row(children: [
      SizedBox(width: 80, child: Text(label,
        style: const TextStyle(color: Colors.white, fontSize: 12))),
      Expanded(child: Slider(value: value, min: min, max: max,
                              onChanged: onChanged)),
      SizedBox(width: 60, child: Text(value.toStringAsFixed(3),
        style: const TextStyle(color: Colors.white, fontSize: 12))),
    ]);
  }
}
