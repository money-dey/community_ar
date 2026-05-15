// main.dart — Phase 2 lip recolor demo.
//
// This app is the visible payoff for Phase 2. When you can drag the
// opacity slider from 0 to 1 and see your lips fade from natural to
// painted red in real time, the entire stack (Phase 0 plumbing + Phase 1
// perception + Phase 2 effect engine) is genuinely working end-to-end.

import 'package:flutter/material.dart';
import 'package:community_ar/community_ar.dart';

void main() => runApp(const Phase2App());

class Phase2App extends StatelessWidget {
  const Phase2App({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'Community AR Phase 2',
        theme: ThemeData.dark(),
        home: const Phase2Home(),
      );
}

class Phase2Home extends StatefulWidget {
  const Phase2Home({super.key});

  @override
  State<Phase2Home> createState() => _Phase2HomeState();
}

class _Phase2HomeState extends State<Phase2Home> {
  CameraLens _camera = CameraLens.front;

  // A small palette of believable lipstick shades, deliberately chosen to
  // span warm/cool/dark/light so you can verify Oklab handles them all.
  static const _palette = <Color>[
    Color(0xFFCC0033), // classic red
    Color(0xFF8E1A38), // deep berry
    Color(0xFFE57373), // soft pink
    Color(0xFFB07060), // nude
    Color(0xFF5D2A2C), // dark wine
    Color(0xFFFF6B6B), // coral
    Color(0xFFD4A5A5), // mauve
    Color(0xFF330000), // very dark / matte
  ];

  Color  _color = _palette[0];
  double _opacity = 0.85;
  double _edgeSoftness = 0.4;
  double _luminancePreserve = 1.0;

  EffectGraph get _graph => EffectGraph(effects: [
        LipsEffect(
          color: _color,
          opacity: _opacity,
          edgeSoftness: _edgeSoftness,
          luminancePreserve: _luminancePreserve,
        ),
      ]);

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

          // Bottom controls overlay
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
                children: [
                  // Color palette row
                  SizedBox(
                    height: 56,
                    child: ListView.separated(
                      scrollDirection: Axis.horizontal,
                      itemCount: _palette.length,
                      separatorBuilder: (_, __) => const SizedBox(width: 12),
                      itemBuilder: (_, i) {
                        final c = _palette[i];
                        final selected = c.value == _color.value;
                        return GestureDetector(
                          onTap: () => setState(() => _color = c),
                          child: Container(
                            width: 56,
                            decoration: BoxDecoration(
                              color: c,
                              shape: BoxShape.circle,
                              border: Border.all(
                                color: selected ? Colors.white : Colors.transparent,
                                width: 3,
                              ),
                            ),
                          ),
                        );
                      },
                    ),
                  ),
                  const SizedBox(height: 12),

                  // Parameter sliders
                  _slider('opacity', _opacity,
                      (v) => setState(() => _opacity = v)),
                  _slider('softness', _edgeSoftness,
                      (v) => setState(() => _edgeSoftness = v)),
                  _slider('preserve brightness', _luminancePreserve,
                      (v) => setState(() => _luminancePreserve = v)),

                  // Camera flip
                  Center(
                    child: TextButton.icon(
                      icon: const Icon(Icons.cameraswitch),
                      label: Text(_camera == CameraLens.front
                          ? 'Use back camera'
                          : 'Use front camera'),
                      onPressed: () => setState(() {
                        _camera = _camera == CameraLens.front
                            ? CameraLens.back
                            : CameraLens.front;
                      }),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _slider(String label, double value, ValueChanged<double> onChanged) {
    return Row(children: [
      SizedBox(width: 140,
          child: Text(label, style: const TextStyle(color: Colors.white, fontSize: 12))),
      Expanded(child: Slider(value: value, onChanged: onChanged)),
      SizedBox(width: 40,
          child: Text(value.toStringAsFixed(2),
              style: const TextStyle(color: Colors.white, fontSize: 12))),
    ]);
  }
}
