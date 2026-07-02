// community_ar_phase0_ffi.dart
// =============================================================================
// Dart FFI bindings for the Phase 0 C ABI.
//
// All native handles are opaque. Method calls go through a Flutter MethodChannel
// for Phase 0 simplicity (Phase 0 is dominated by platform-channel work for
// camera permission/lifecycle anyway). Later phases switch direct ABI calls
// to dart:ffi for hot-path operations.
// =============================================================================

import 'dart:async';
import 'package:flutter/services.dart';

enum CARTestMode { passthrough, grayscale, invert, vignette }

class CARPhase0Stats {
  final double avgFrameTimeMs;
  final int framesProcessed;
  final int framesDropped;

  const CARPhase0Stats({
    required this.avgFrameTimeMs,
    required this.framesProcessed,
    required this.framesDropped,
  });

  factory CARPhase0Stats.fromMap(Map<dynamic, dynamic>? m) {
    if (m == null) {
      return const CARPhase0Stats(
          avgFrameTimeMs: 0, framesProcessed: 0, framesDropped: 0);
    }
    return CARPhase0Stats(
      avgFrameTimeMs: (m['avgFrameTimeMs'] as num? ?? 0).toDouble(),
      framesProcessed: (m['framesProcessed'] as num? ?? 0).toInt(),
      framesDropped: (m['framesDropped'] as num? ?? 0).toInt(),
    );
  }
}

class CommunityARPhase0FFI {
  CommunityARPhase0FFI._();

  static const _channel = MethodChannel('dev.communityar/methods');

  /// Requests the runtime CAMERA permission (Android 6+). Returns true if
  /// granted. Handled natively by the plugin (no extra Dart dependency).
  static Future<bool> requestCameraPermission() async {
    final granted =
        await _channel.invokeMethod<bool>('requestCameraPermission');
    return granted ?? false;
  }

  static Future<int> createSession() async {
    final id = await _channel.invokeMethod<int>('createSession');
    return id ?? -1;
  }

  static Future<void> startCamera({required String lens}) async {
    await _channel.invokeMethod('startCamera', {'lens': lens});
  }

  static Future<void> switchCamera({required String lens}) async {
    await _channel.invokeMethod('switchCamera', {'lens': lens});
  }

  static Future<int> getOutputTextureId() async {
    final id = await _channel.invokeMethod<int>('outputTextureId');
    return id ?? -1;
  }

  static Future<({int width, int height})> getOutputDimensions() async {
    final m = await _channel.invokeMethod<Map>('outputDimensions');
    return (
      width: (m?['width'] as num? ?? 0).toInt(),
      height: (m?['height'] as num? ?? 0).toInt()
    );
  }

  static Future<void> setTestMode(CARTestMode mode) async {
    await _channel.invokeMethod('setTestMode', {'mode': mode.index});
  }

  static Future<CARPhase0Stats> getStats() async {
    final m = await _channel.invokeMethod<Map>('getStats');
    return CARPhase0Stats.fromMap(m);
  }

  static Future<void> dispose() async {
    await _channel.invokeMethod('dispose');
  }
}
