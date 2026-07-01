// community_ar_phase1_ffi.dart
// =============================================================================
// Phase 1 Dart-side additions: debug overlay control, perception stats,
// filter tuning.
// =============================================================================

import 'package:flutter/services.dart';

class DebugOverlayMode {
  static const int none      = 0;
  static const int landmarks = 1 << 0;
  static const int mesh      = 1 << 1;
  static const int iris      = 1 << 2;
  static const int hairMask  = 1 << 3;
  static const int pose      = 1 << 4;
  static const int skinTone  = 1 << 5;
  static const int all       = -1;

  static int combine(List<int> modes) => modes.fold(0, (a, b) => a | b);
}

class CARPerceptionStats {
  final int facesDetected;
  final double faceMeshInferenceMs;
  final double irisInferenceMs;
  final double hairSegInferenceMs;
  final double pnpSolveMs;
  final int activeFilterCount;
  final double skinBaselineLuma;
  final bool skinToneValid;

  const CARPerceptionStats({
    required this.facesDetected,
    required this.faceMeshInferenceMs,
    required this.irisInferenceMs,
    required this.hairSegInferenceMs,
    required this.pnpSolveMs,
    required this.activeFilterCount,
    required this.skinBaselineLuma,
    required this.skinToneValid,
  });

  factory CARPerceptionStats.fromMap(Map<dynamic, dynamic>? m) {
    if (m == null) {
      return const CARPerceptionStats(
        facesDetected: 0, faceMeshInferenceMs: 0, irisInferenceMs: 0,
        hairSegInferenceMs: 0, pnpSolveMs: 0, activeFilterCount: 0,
        skinBaselineLuma: 0, skinToneValid: false,
      );
    }
    return CARPerceptionStats(
      facesDetected:       (m['facesDetected'] as num? ?? 0).toInt(),
      faceMeshInferenceMs: (m['faceMeshInferenceMs'] as num? ?? 0).toDouble(),
      irisInferenceMs:     (m['irisInferenceMs'] as num? ?? 0).toDouble(),
      hairSegInferenceMs:  (m['hairSegInferenceMs'] as num? ?? 0).toDouble(),
      pnpSolveMs:          (m['pnpSolveMs'] as num? ?? 0).toDouble(),
      activeFilterCount:   (m['activeFilterCount'] as num? ?? 0).toInt(),
      skinBaselineLuma:    (m['skinBaselineLuma'] as num? ?? 0).toDouble(),
      skinToneValid:       (m['skinToneValid'] as bool? ?? false),
    );
  }
}

class CARPerceptionRequest {
  final bool needFaceLandmarks;
  final bool needIris;
  final bool needHair;
  final bool needSelfieSeg;
  final bool needPose;
  final bool needSkinTone;

  const CARPerceptionRequest({
    this.needFaceLandmarks = false,
    this.needIris = false,
    this.needHair = false,
    this.needSelfieSeg = false,
    this.needPose = false,
    this.needSkinTone = false,
  });

  Map<String, int> toMap() => {
    'needFaceLandmarks': needFaceLandmarks ? 1 : 0,
    'needIris':          needIris ? 1 : 0,
    'needHair':          needHair ? 1 : 0,
    'needSelfieSeg':     needSelfieSeg ? 1 : 0,
    'needPose':          needPose ? 1 : 0,
    'needSkinTone':      needSkinTone ? 1 : 0,
  };
}

class CommunityARPhase1FFI {
  CommunityARPhase1FFI._();

  static const _channel = MethodChannel('dev.communityar/methods');

  static Future<void> setDebugOverlay(int modeMask) =>
      _channel.invokeMethod('setDebugOverlay', {'mode': modeMask});

  static Future<void> setOneEuroParams({
    required double minCutoff,
    required double beta,
    required double dCutoff,
  }) => _channel.invokeMethod('setOneEuroParams', {
        'minCutoff': minCutoff, 'beta': beta, 'dCutoff': dCutoff,
      });

  static Future<CARPerceptionStats> getPerceptionStats() async {
    final m = await _channel.invokeMethod<Map>('getPerceptionStats');
    return CARPerceptionStats.fromMap(m);
  }

  static Future<void> forcePerception(CARPerceptionRequest req) =>
      _channel.invokeMethod('forcePerception', req.toMap());
}
