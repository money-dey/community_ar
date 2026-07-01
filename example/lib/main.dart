// main.dart — Community AR showcase / device-test harness.
//
// This example is intentionally comprehensive: it exercises every capability
// the public Dart API currently exposes, so a tester can validate each feature
// on a real device from one screen.
//
// What it demonstrates:
//   - Camera pipeline with front/back switching (CommunityARView, CameraLens)
//   - Effect composition with automatic pass-ordering (EffectGraph)
//   - SkinSmoothEffect: all 9 presets, explicit quality-tier selection, and
//     live sliders for every BeautyFilterConfig knob
//   - LipsEffect: color palette + opacity / edge-softness / luminance sliders
//   - Debug overlays: landmarks, mesh, iris, hair mask, pose, skin tone
//     (CommunityARPhase1FFI.setDebugOverlay + forcePerception)
//   - One-Euro landmark filter tuning (setOneEuroParams)
//   - Live perception stats HUD (getPerceptionStats) + active mask-pool names
//     (getMaskPoolNames) + resolved beauty quality (getBeautyEffectiveQuality)
//
// Note: several capabilities depend on native perception/beauty running on a
// real device; in this harness the controls are always present so each can be
// verified as the native side comes online.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:community_ar/community_ar.dart';

void main() => runApp(const ShowcaseApp());

class ShowcaseApp extends StatelessWidget {
  const ShowcaseApp({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'Community AR — Showcase',
        theme: ThemeData.dark(useMaterial3: true),
        debugShowCheckedModeBanner: false,
        home: const ShowcaseHome(),
      );
}

class ShowcaseHome extends StatefulWidget {
  const ShowcaseHome({super.key});

  @override
  State<ShowcaseHome> createState() => _ShowcaseHomeState();
}

class _ShowcaseHomeState extends State<ShowcaseHome> {
  // ---- Camera ----
  CameraLens _camera = CameraLens.front;

  // ---- Beauty ----
  bool _beautyEnabled = true;
  String _presetName = 'natural';
  // Working config, seeded from the selected preset and mutated by the sliders.
  BeautyFilterConfig _beauty = BeautyPresets.natural;
  BeautyQuality _quality = BeautyQuality.auto; // explicit tier choice
  BeautyQuality? _effectiveQuality; // resolved tier (polled)

  // ---- Lipstick ----
  static const _palette = <Color>[
    Color(0xFFCC0033), // classic red
    Color(0xFF8E1A38), // deep berry
    Color(0xFFE57373), // soft pink
    Color(0xFFB07060), // nude
    Color(0xFF5D2A2C), // dark wine
    Color(0xFFFF6B6B), // coral
    Color(0xFFD4A5A5), // mauve
    Color(0xFF330000), // matte
  ];
  bool _lipstickEnabled = true;
  Color _lipColor = _palette[0];
  double _lipOpacity = 0.85;
  double _lipSoftness = 0.4;
  double _lipLuma = 1.0;

  // ---- Debug overlays + filter tuning ----
  static const _overlays = <(String, int)>[
    ('Landmarks', DebugOverlayMode.landmarks),
    ('Mesh', DebugOverlayMode.mesh),
    ('Iris', DebugOverlayMode.iris),
    ('Hair mask', DebugOverlayMode.hairMask),
    ('Pose', DebugOverlayMode.pose),
    ('Skin tone', DebugOverlayMode.skinTone),
  ];
  final Set<int> _overlayModes = <int>{};
  double _minCutoff = 1.0;
  double _beta = 0.007;

  // ---- Stats HUD ----
  bool _statsVisible = true;
  CARPerceptionStats? _stats;
  List<String> _maskNames = const <String>[];
  Timer? _poll;

  // ---- Bottom panel tab ----
  int _tab = 0; // 0 = Beauty, 1 = Lips, 2 = Debug

  @override
  void initState() {
    super.initState();
    _applyDebugOverlay();
    _applyOneEuro();
    _poll = Timer.periodic(const Duration(seconds: 1), (_) => _refreshDiagnostics());
  }

  @override
  void dispose() {
    _poll?.cancel();
    super.dispose();
  }

  // --------------------------------------------------------------------------
  // Effect graph
  // --------------------------------------------------------------------------
  EffectGraph get _graph {
    final effects = <Effect>[];
    if (_beautyEnabled) {
      // The quality picker overrides whatever tier the preset carried.
      effects.add(SkinSmoothEffect(config: _beauty.copyWith(quality: _quality)));
    }
    if (_lipstickEnabled) {
      effects.add(LipsEffect(
        color: _lipColor,
        opacity: _lipOpacity,
        edgeSoftness: _lipSoftness,
        luminancePreserve: _lipLuma,
      ));
    }
    return EffectGraph(effects: effects);
  }

  // --------------------------------------------------------------------------
  // Native diagnostic wiring (all fire-and-forget; failures are non-fatal)
  // --------------------------------------------------------------------------
  Future<void> _refreshDiagnostics() async {
    if (!mounted) return;
    try {
      final stats = await CommunityARPhase1FFI.getPerceptionStats();
      final masks = await CommunityARPhase3FFI.getMaskPoolNames();
      final q = await CommunityARPhase3FFI.getBeautyEffectiveQuality();
      if (!mounted) return;
      setState(() {
        _stats = stats;
        _maskNames = masks;
        _effectiveQuality = q;
      });
    } catch (_) {
      // No native session yet, or no effect installed — keep last values.
    }
  }

  // Swallow errors on fire-and-forget native calls so a device without the
  // method handler wired yet doesn't spam unhandled PlatformExceptions.
  void _fireAndForget(Future<void> f) => f.catchError((_) {});

  void _applyDebugOverlay() {
    final mask = DebugOverlayMode.combine(_overlayModes.toList());
    _fireAndForget(CommunityARPhase1FFI.setDebugOverlay(mask));

    // Force the perception each active overlay depends on, so the overlays
    // have data to draw even when no effect requires that model this frame.
    final needLandmarks = _overlayModes.contains(DebugOverlayMode.landmarks) ||
        _overlayModes.contains(DebugOverlayMode.mesh) ||
        _overlayModes.contains(DebugOverlayMode.iris) ||
        _overlayModes.contains(DebugOverlayMode.pose) ||
        _overlayModes.contains(DebugOverlayMode.skinTone);
    _fireAndForget(CommunityARPhase1FFI.forcePerception(CARPerceptionRequest(
      needFaceLandmarks: needLandmarks,
      needIris: _overlayModes.contains(DebugOverlayMode.iris),
      needHair: _overlayModes.contains(DebugOverlayMode.hairMask),
      needPose: _overlayModes.contains(DebugOverlayMode.pose),
      needSkinTone: _overlayModes.contains(DebugOverlayMode.skinTone),
    )));
  }

  void _applyOneEuro() {
    _fireAndForget(CommunityARPhase1FFI.setOneEuroParams(
      minCutoff: _minCutoff,
      beta: _beta,
      dCutoff: 1.0,
    ));
  }

  // --------------------------------------------------------------------------
  // Build
  // --------------------------------------------------------------------------
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Stack(
        children: [
          Positioned.fill(
            child: CommunityARView(
              camera: _camera,
              effects: _graph,
              fit: BoxFit.cover,
            ),
          ),
          _topBar(),
          if (_statsVisible) _statsHud(),
          _bottomPanel(),
        ],
      ),
    );
  }

  // ---- Top bar: effect toggles, quality badge, camera + stats toggles ----
  Widget _topBar() {
    return Positioned(
      left: 0,
      right: 0,
      top: 0,
      child: SafeArea(
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          color: Colors.black54,
          child: Row(
            children: [
              _toggleChip('Beauty', _beautyEnabled,
                  (v) => setState(() => _beautyEnabled = v)),
              const SizedBox(width: 8),
              _toggleChip('Lips', _lipstickEnabled,
                  (v) => setState(() => _lipstickEnabled = v)),
              const SizedBox(width: 10),
              if (_beautyEnabled && _effectiveQuality != null)
                _qualityBadge(_effectiveQuality!),
              const Spacer(),
              IconButton(
                tooltip: 'Toggle stats',
                icon: Icon(_statsVisible
                    ? Icons.query_stats
                    : Icons.query_stats_outlined),
                color: _statsVisible ? Colors.tealAccent : Colors.white70,
                onPressed: () => setState(() => _statsVisible = !_statsVisible),
              ),
              IconButton(
                tooltip: 'Switch camera',
                icon: const Icon(Icons.cameraswitch),
                color: Colors.white,
                onPressed: () => setState(() {
                  _camera = _camera == CameraLens.front
                      ? CameraLens.back
                      : CameraLens.front;
                }),
              ),
            ],
          ),
        ),
      ),
    );
  }

  // ---- Perception stats HUD ----
  Widget _statsHud() {
    final s = _stats;
    return Positioned(
      left: 12,
      top: 92,
      child: IgnorePointer(
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
          decoration: BoxDecoration(
            color: Colors.black.withOpacity(0.55),
            borderRadius: BorderRadius.circular(8),
          ),
          child: DefaultTextStyle(
            style: const TextStyle(
                color: Colors.tealAccent, fontSize: 11, height: 1.4,
                fontFeatures: [FontFeature.tabularFigures()]),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text('faces: ${s?.facesDetected ?? '-'}   '
                    'filters: ${s?.activeFilterCount ?? '-'}'),
                Text('faceMesh: ${_ms(s?.faceMeshInferenceMs)}   '
                    'iris: ${_ms(s?.irisInferenceMs)}'),
                Text('hairSeg: ${_ms(s?.hairSegInferenceMs)}   '
                    'pnp: ${_ms(s?.pnpSolveMs)}'),
                Text('skinLuma: ${s == null ? '-' : s.skinBaselineLuma.toStringAsFixed(2)}'
                    ' ${s?.skinToneValid == true ? '✓' : '·'}'),
                if (_maskNames.isNotEmpty)
                  Text('masks: ${_maskNames.map(_shortMask).join(', ')}',
                      style: const TextStyle(color: Colors.white70, fontSize: 10)),
              ],
            ),
          ),
        ),
      ),
    );
  }

  String _ms(double? v) => v == null ? '-' : '${v.toStringAsFixed(1)}ms';
  String _shortMask(String n) => n.replaceFirst('masks.', '');

  // ---- Bottom panel with tabs ----
  Widget _bottomPanel() {
    return Positioned(
      left: 0,
      right: 0,
      bottom: 0,
      child: Container(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: [Colors.black.withOpacity(0), Colors.black87],
          ),
        ),
        padding: const EdgeInsets.fromLTRB(12, 10, 12, 16),
        child: SafeArea(
          top: false,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              _tabBar(),
              const SizedBox(height: 8),
              SizedBox(
                height: 226,
                child: IndexedStack(
                  index: _tab,
                  children: [
                    _beautyTab(),
                    _lipsTab(),
                    _debugTab(),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _tabBar() {
    const labels = ['Beauty', 'Lips', 'Debug'];
    return Row(
      children: [
        for (var i = 0; i < labels.length; i++) ...[
          if (i > 0) const SizedBox(width: 8),
          Expanded(
            child: GestureDetector(
              onTap: () => setState(() => _tab = i),
              child: Container(
                padding: const EdgeInsets.symmetric(vertical: 8),
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: _tab == i ? Colors.tealAccent.withOpacity(0.2) : Colors.white10,
                  border: Border.all(
                    color: _tab == i ? Colors.tealAccent : Colors.transparent,
                  ),
                  borderRadius: BorderRadius.circular(10),
                ),
                child: Text(labels[i],
                    style: TextStyle(
                      color: _tab == i ? Colors.tealAccent : Colors.white70,
                      fontWeight: FontWeight.w600,
                      fontSize: 13,
                    )),
              ),
            ),
          ),
        ],
      ],
    );
  }

  // ---- Beauty tab ----
  Widget _beautyTab() {
    if (!_beautyEnabled) {
      return _disabledHint('Beauty is off — enable it in the top bar.');
    }
    final b = _beauty;
    return ListView(
      padding: EdgeInsets.zero,
      children: [
        _label('Preset'),
        SizedBox(
          height: 36,
          child: ListView.separated(
            scrollDirection: Axis.horizontal,
            itemCount: BeautyPresets.all.length,
            separatorBuilder: (_, __) => const SizedBox(width: 6),
            itemBuilder: (_, i) {
              final e = BeautyPresets.all[i];
              return ChoiceChip(
                label: Text(e.key),
                selected: e.key == _presetName,
                onSelected: (_) => setState(() {
                  _presetName = e.key;
                  _beauty = e.value;
                  _quality = e.value.quality;
                }),
              );
            },
          ),
        ),
        const SizedBox(height: 6),
        _label('Quality tier'),
        Wrap(
          spacing: 6,
          children: [
            for (final q in BeautyQuality.values)
              ChoiceChip(
                label: Text(q.name),
                selected: _quality == q,
                onSelected: (_) => setState(() => _quality = q),
              ),
          ],
        ),
        const SizedBox(height: 4),
        _label('Parameters (live)'),
        _slider('smoothingStrength', b.smoothingStrength, 0, 1,
            (v) => setState(() => _beauty = b.copyWith(smoothingStrength: v))),
        _slider('detailPreserve', b.detailPreserve, 0.5, 1,
            (v) => setState(() => _beauty = b.copyWith(detailPreserve: v))),
        _slider('blemishReduction', b.blemishReduction, 0, 1,
            (v) => setState(() => _beauty = b.copyWith(blemishReduction: v))),
        _slider('bilateralEdgeSensitivity', b.bilateralEdgeSensitivity, 0, 1,
            (v) => setState(() => _beauty = b.copyWith(bilateralEdgeSensitivity: v))),
        _slider('highFreqStrength', b.highFreqStrength, 0, 1,
            (v) => setState(() => _beauty = b.copyWith(highFreqStrength: v))),
        _slider('midFreqStrength', b.midFreqStrength, 0, 1,
            (v) => setState(() => _beauty = b.copyWith(midFreqStrength: v))),
        _slider('specularControl (matte↔glow)', b.specularControl, -1, 1,
            (v) => setState(() => _beauty = b.copyWith(specularControl: v))),
        _slider('warmth', b.warmth, -0.2, 0.2,
            (v) => setState(() => _beauty = b.copyWith(warmth: v))),
        _slider('highlightLift', b.highlightLift, 0, 0.3,
            (v) => setState(() => _beauty = b.copyWith(highlightLift: v))),
        _slider('clarity', b.clarity, 0, 0.5,
            (v) => setState(() => _beauty = b.copyWith(clarity: v))),
        _slider('temporalSmoothing', b.temporalSmoothing, 0, 1,
            (v) => setState(() => _beauty = b.copyWith(temporalSmoothing: v))),
        _slider('adaptivenessLocal', b.adaptivenessLocal, 0, 1,
            (v) => setState(() => _beauty = b.copyWith(adaptivenessLocal: v))),
      ],
    );
  }

  // ---- Lips tab ----
  Widget _lipsTab() {
    if (!_lipstickEnabled) {
      return _disabledHint('Lipstick is off — enable it in the top bar.');
    }
    return ListView(
      padding: EdgeInsets.zero,
      children: [
        _label('Color'),
        SizedBox(
          height: 44,
          child: ListView.separated(
            scrollDirection: Axis.horizontal,
            itemCount: _palette.length,
            separatorBuilder: (_, __) => const SizedBox(width: 10),
            itemBuilder: (_, i) {
              final c = _palette[i];
              final sel = c.value == _lipColor.value;
              return GestureDetector(
                onTap: () => setState(() => _lipColor = c),
                child: Container(
                  width: 44,
                  decoration: BoxDecoration(
                    color: c,
                    shape: BoxShape.circle,
                    border: Border.all(
                      color: sel ? Colors.white : Colors.transparent,
                      width: 3,
                    ),
                  ),
                ),
              );
            },
          ),
        ),
        const SizedBox(height: 6),
        _label('Parameters'),
        _slider('opacity', _lipOpacity, 0, 1,
            (v) => setState(() => _lipOpacity = v)),
        _slider('edgeSoftness', _lipSoftness, 0, 1,
            (v) => setState(() => _lipSoftness = v)),
        _slider('luminancePreserve (natural↔flat)', _lipLuma, 0, 1,
            (v) => setState(() => _lipLuma = v)),
      ],
    );
  }

  // ---- Debug tab ----
  Widget _debugTab() {
    return ListView(
      padding: EdgeInsets.zero,
      children: [
        _label('Debug overlays'),
        Wrap(
          spacing: 6,
          runSpacing: 2,
          children: [
            for (final (name, bit) in _overlays)
              FilterChip(
                label: Text(name),
                selected: _overlayModes.contains(bit),
                onSelected: (on) => setState(() {
                  if (on) {
                    _overlayModes.add(bit);
                  } else {
                    _overlayModes.remove(bit);
                  }
                  _applyDebugOverlay();
                }),
              ),
          ],
        ),
        const SizedBox(height: 6),
        _label('One-Euro landmark filter'),
        _slider('minCutoff', _minCutoff, 0, 3, (v) {
          setState(() => _minCutoff = v);
          _applyOneEuro();
        }),
        _slider('beta', _beta, 0, 0.05, (v) {
          setState(() => _beta = v);
          _applyOneEuro();
        }, decimals: 3),
        const SizedBox(height: 4),
        _label('Active mask pool'),
        Text(
          _maskNames.isEmpty ? '(none this frame)' : _maskNames.join('\n'),
          style: const TextStyle(color: Colors.white70, fontSize: 11),
        ),
      ],
    );
  }

  // --------------------------------------------------------------------------
  // Small reusable widgets
  // --------------------------------------------------------------------------
  Widget _label(String s) => Padding(
        padding: const EdgeInsets.only(top: 6, bottom: 4),
        child: Text(s,
            style: const TextStyle(color: Colors.white70, fontSize: 12)),
      );

  Widget _slider(String label, double value, double min, double max,
      ValueChanged<double> onChanged,
      {int decimals = 2}) {
    return Row(
      children: [
        SizedBox(
          width: 150,
          child: Text(label,
              style: const TextStyle(color: Colors.white, fontSize: 11),
              overflow: TextOverflow.ellipsis),
        ),
        Expanded(
          child: Slider(
            value: value.clamp(min, max),
            min: min,
            max: max,
            onChanged: onChanged,
          ),
        ),
        SizedBox(
          width: 44,
          child: Text(value.toStringAsFixed(decimals),
              textAlign: TextAlign.right,
              style: const TextStyle(color: Colors.white70, fontSize: 11)),
        ),
      ],
    );
  }

  Widget _toggleChip(String label, bool value, ValueChanged<bool> onChanged) =>
      FilterChip(
        label: Text(label),
        selected: value,
        onSelected: onChanged,
      );

  Widget _disabledHint(String s) => Center(
        child: Text(s,
            style: const TextStyle(color: Colors.white38, fontSize: 12)),
      );

  Widget _qualityBadge(BeautyQuality q) {
    final (label, color) = switch (q) {
      BeautyQuality.high => ('HIGH', Colors.greenAccent),
      BeautyQuality.medium => ('MEDIUM', Colors.amberAccent),
      BeautyQuality.low => ('LOW', Colors.redAccent),
      BeautyQuality.auto => ('AUTO…', Colors.white70),
    };
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: color.withOpacity(0.18),
        border: Border.all(color: color, width: 1),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Text(label,
          style: TextStyle(
              color: color, fontSize: 11, fontWeight: FontWeight.bold)),
    );
  }
}
