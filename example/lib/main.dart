// main.dart — Phase 3 demo: beauty preset picker + lipstick.
//
// What this demonstrates:
//   - SkinSmoothEffect and LipsEffect composed in one graph
//   - Effect ordering is automatic (passOrder sorts beauty before recolor)
//   - Preset picker shows the 9 named looks
//   - Adjustable lipstick color via palette
//
// In Batch 3 the beauty pipeline is a passthrough (no shaders yet), so
// the visible effect of toggling presets is the FPS counter and the
// debug overlay's mask list — the pixels themselves don't change yet.
// When Batch 4 lands, the same UI immediately shows real beauty results.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:community_ar/community_ar.dart';

void main() => runApp(const Phase3App());

class Phase3App extends StatelessWidget {
  const Phase3App({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'Community AR Phase 3',
        theme: ThemeData.dark(),
        home: const Phase3Home(),
      );
}

class Phase3Home extends StatefulWidget {
  const Phase3Home({super.key});

  @override
  State<Phase3Home> createState() => _Phase3HomeState();
}

class _Phase3HomeState extends State<Phase3Home> {
  CameraLens _camera = CameraLens.front;

  // Beauty state
  String _presetName = 'natural';
  BeautyFilterConfig get _beautyConfig => BeautyPresets.all
      .firstWhere((e) => e.key == _presetName,
                  orElse: () => BeautyPresets.all.first)
      .value;

  // Effective quality polling — refresh every second so user sees Auto
  // resolve to High/Medium/Low after the benchmark completes.
  BeautyQuality? _effectiveQuality;
  Timer? _qualityPoll;

  @override
  void initState() {
    super.initState();
    _qualityPoll = Timer.periodic(const Duration(seconds: 1), (_) async {
      if (!mounted) return;
      try {
        final q = await CommunityARPhase3FFI.getBeautyEffectiveQuality();
        if (mounted) setState(() => _effectiveQuality = q);
      } catch (_) {
        // FFI failures are silent; happens if no beauty effect installed.
      }
    });
  }

  @override
  void dispose() {
    _qualityPoll?.cancel();
    super.dispose();
  }

  // Lipstick state — deliberately mirrors Phase 2's palette to verify
  // backward compatibility
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
  Color _lipColor = _palette[0];
  double _lipOpacity = 0.85;

  bool _beautyEnabled = true;
  bool _lipstickEnabled = true;

  EffectGraph get _graph {
    final effects = <Effect>[];
    if (_beautyEnabled) {
      effects.add(SkinSmoothEffect(config: _beautyConfig));
    }
    if (_lipstickEnabled) {
      effects.add(LipsEffect(color: _lipColor, opacity: _lipOpacity));
    }
    return EffectGraph(effects: effects);
  }

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

          // Top bar — effect toggles
          Positioned(
            left: 0, right: 0, top: 0,
            child: SafeArea(
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                color: Colors.black54,
                child: Row(
                  children: [
                    _toggleChip(
                      'Beauty',
                      _beautyEnabled,
                      (v) => setState(() => _beautyEnabled = v),
                    ),
                    const SizedBox(width: 8),
                    _toggleChip(
                      'Lipstick',
                      _lipstickEnabled,
                      (v) => setState(() => _lipstickEnabled = v),
                    ),
                    const SizedBox(width: 12),
                    if (_beautyEnabled && _effectiveQuality != null)
                      _qualityBadge(_effectiveQuality!),
                    const Spacer(),
                    IconButton(
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
          ),

          // Bottom panel — preset picker + lipstick controls
          Positioned(
            left: 0, right: 0, bottom: 0,
            child: Container(
              padding: const EdgeInsets.fromLTRB(16, 12, 16, 24),
              decoration: BoxDecoration(
                gradient: LinearGradient(
                  begin: Alignment.topCenter,
                  end: Alignment.bottomCenter,
                  colors: [Colors.black.withOpacity(0), Colors.black87],
                ),
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  if (_beautyEnabled) ...[
                    const Text('Beauty preset',
                        style: TextStyle(color: Colors.white70, fontSize: 12)),
                    SizedBox(
                      height: 40,
                      child: ListView.separated(
                        scrollDirection: Axis.horizontal,
                        itemCount: BeautyPresets.all.length,
                        separatorBuilder: (_, __) => const SizedBox(width: 8),
                        itemBuilder: (_, i) {
                          final entry = BeautyPresets.all[i];
                          final selected = entry.key == _presetName;
                          return ChoiceChip(
                            label: Text(entry.key),
                            selected: selected,
                            onSelected: (_) => setState(() => _presetName = entry.key),
                          );
                        },
                      ),
                    ),
                    const SizedBox(height: 12),
                  ],

                  if (_lipstickEnabled) ...[
                    const Text('Lip color',
                        style: TextStyle(color: Colors.white70, fontSize: 12)),
                    SizedBox(
                      height: 48,
                      child: ListView.separated(
                        scrollDirection: Axis.horizontal,
                        itemCount: _palette.length,
                        separatorBuilder: (_, __) => const SizedBox(width: 12),
                        itemBuilder: (_, i) {
                          final c = _palette[i];
                          final sel = c.value == _lipColor.value;
                          return GestureDetector(
                            onTap: () => setState(() => _lipColor = c),
                            child: Container(
                              width: 48,
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
                    Row(children: [
                      const SizedBox(width: 8,
                          child: Text('Opacity',
                              style: TextStyle(color: Colors.white70, fontSize: 12))),
                      Expanded(
                        child: Slider(
                          value: _lipOpacity,
                          onChanged: (v) => setState(() => _lipOpacity = v),
                        ),
                      ),
                      SizedBox(width: 36,
                          child: Text(_lipOpacity.toStringAsFixed(2),
                              style: const TextStyle(color: Colors.white, fontSize: 12))),
                    ]),
                  ],
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _toggleChip(String label, bool value, ValueChanged<bool> onChanged) {
    return FilterChip(
      label: Text(label),
      selected: value,
      onSelected: onChanged,
    );
  }

  // Small visual indicator showing the resolved quality tier. Most useful
  // when the user has selected BeautyQuality.auto (the preset's default)
  // and is curious which tier the benchmark settled on.
  Widget _qualityBadge(BeautyQuality q) {
    final (label, color) = switch (q) {
      BeautyQuality.high   => ('HIGH',   Colors.greenAccent),
      BeautyQuality.medium => ('MEDIUM', Colors.amberAccent),
      BeautyQuality.low    => ('LOW',    Colors.redAccent),
      BeautyQuality.auto   => ('AUTO…',  Colors.white70),
    };
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: color.withOpacity(0.18),
        border: Border.all(color: color, width: 1),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Text(label,
          style: TextStyle(color: color, fontSize: 11,
                            fontWeight: FontWeight.bold)),
    );
  }
}
