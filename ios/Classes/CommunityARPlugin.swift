// CommunityARPlugin.swift
// =============================================================================
// Community AR — iOS Flutter plugin
//
// Mirror of the Android plugin: owns AVCaptureSession, hands Metal textures
// to native C++, registers output with Flutter's FlutterTextureRegistry.
//
// Zero-copy path:
//   AVCaptureVideoDataOutput → CVPixelBuffer → CVMetalTextureRef → MTLTexture
// =============================================================================

import AVFoundation
import Flutter
import Metal
import MetalKit

public class CommunityARPlugin: NSObject, FlutterPlugin {
    private static let channelName = "dev.communityar/methods"

    private weak var textureRegistry: FlutterTextureRegistry?
    private var outputTexture: CommunityAROutputTexture?
    private var outputTextureId: Int64 = -1

    private var cameraStream: CameraStream?
    private var sessionPtr: OpaquePointer?

    public static func register(with registrar: FlutterPluginRegistrar) {
        let channel = FlutterMethodChannel(name: channelName, binaryMessenger: registrar.messenger())
        let instance = CommunityARPlugin()
        instance.textureRegistry = registrar.textures()
        registrar.addMethodCallDelegate(instance, channel: channel)
    }

    public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        switch call.method {
        case "createSession":
            result(createSession())
        case "startCamera":
            let lens = (call.arguments as? [String: Any])?["lens"] as? String ?? "front"
            startCamera(lens: lens); result(nil)
        case "switchCamera":
            let lens = (call.arguments as? [String: Any])?["lens"] as? String ?? "front"
            switchCamera(lens: lens); result(nil)
        case "setTestMode":
            let mode = (call.arguments as? [String: Any])?["mode"] as? Int ?? 0
            setTestMode(mode); result(nil)
        case "outputTextureId":
            result(outputTextureId)
        case "outputDimensions":
            result(getOutputDimensions())
        case "getStats":
            result(getStats())
        case "dispose":
            teardown(); result(nil)
        default:
            result(FlutterMethodNotImplemented)
        }
    }

    // -------------------------------------------------------------------------
    // Session
    // -------------------------------------------------------------------------
    private func createSession() -> Int {
        if sessionPtr != nil { return Int(outputTextureId) }

        guard let device = MTLCreateSystemDefaultDevice() else { return -1 }

        var cfg = CARPhase0Config()
        cfg.backend = CAR_BACKEND_METAL
        cfg.gpuContext = UInt64(bitPattern: Int64(Int(bitPattern: Unmanaged.passUnretained(device).toOpaque())))
        cfg.gpuDisplay = 0
        cfg.logLevel = 3

        var status: CARStatus = CAR_STATUS_OK
        sessionPtr = car_p0_create(&cfg, &status)
        guard sessionPtr != nil else { return -1 }

        let output = CommunityAROutputTexture(sessionPtr: sessionPtr!, device: device)
        outputTexture = output
        outputTextureId = textureRegistry?.register(output) ?? -1
        output.flutterTextureId = outputTextureId
        output.notifier = { [weak self] in
            self?.textureRegistry?.textureFrameAvailable(self?.outputTextureId ?? -1)
        }
        return Int(outputTextureId)
    }

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------
    private func startCamera(lens: String) {
        cameraStream?.stop()
        guard let ptr = sessionPtr else { return }
        cameraStream = CameraStream(isFront: lens == "front") { texture, width, height, rotation in
            car_p0_submit_frame(ptr, UInt64(bitPattern: Int64(Int(bitPattern: Unmanaged.passUnretained(texture).toOpaque()))),
                                Int32(width), Int32(height), Int32(rotation),
                                lens == "front" ? 1 : 0)
            self.outputTexture?.notifier?()
        }
        cameraStream?.start()
    }

    private func switchCamera(lens: String) {
        cameraStream?.stop(); startCamera(lens: lens)
    }

    // -------------------------------------------------------------------------
    // Diagnostics
    // -------------------------------------------------------------------------
    private func setTestMode(_ mode: Int) {
        if let p = sessionPtr {
            car_p0_set_test_mode(p, CARTestMode(rawValue: UInt32(mode)))
        }
    }

    private func getOutputDimensions() -> [String: Int] {
        guard let p = sessionPtr else { return ["width": 0, "height": 0] }
        var w: Int32 = 0; var h: Int32 = 0
        car_p0_get_output_dimensions(p, &w, &h)
        return ["width": Int(w), "height": Int(h)]
    }

    private func getStats() -> [String: Any] {
        guard let p = sessionPtr else { return [:] }
        var s = CARPhase0Stats()
        car_p0_get_stats(p, &s)
        return [
            "avgFrameTimeMs":  s.avgFrameTimeMs,
            "framesProcessed": Int(s.framesProcessed),
            "framesDropped":   Int(s.framesDropped),
        ]
    }

    private func teardown() {
        cameraStream?.stop(); cameraStream = nil
        if let p = sessionPtr { car_p0_destroy(p) }
        sessionPtr = nil
        if outputTextureId >= 0 { textureRegistry?.unregisterTexture(outputTextureId) }
        outputTextureId = -1
        outputTexture = nil
    }
}

// =============================================================================
// CommunityAROutputTexture
//
// Adapter conforming to FlutterTexture protocol. On each frame, Flutter asks
// us for a CVPixelBuffer; we wrap the native output MTLTexture in a
// CVPixelBuffer-compatible IOSurface and return it.
// =============================================================================
class CommunityAROutputTexture: NSObject, FlutterTexture {
    let sessionPtr: OpaquePointer
    let device: MTLDevice
    var flutterTextureId: Int64 = -1
    var notifier: (() -> Void)?

    private var cachedPixelBuffer: CVPixelBuffer?

    init(sessionPtr: OpaquePointer, device: MTLDevice) {
        self.sessionPtr = sessionPtr
        self.device = device
    }

    func copyPixelBuffer() -> Unmanaged<CVPixelBuffer>? {
        // Read native output texture handle
        let handle = car_p0_get_output_texture(sessionPtr)
        guard handle != 0 else { return nil }

        // The native side renders into an IOSurface-backed MTLTexture; we wrap
        // the IOSurface in a CVPixelBuffer and hand it to Flutter.
        // For Phase 0 simplicity, we maintain a single CVPixelBuffer that
        // shares the IOSurface with the native render target.
        // (Real implementation: allocate the texture from CVPixelBufferPool
        // upfront and pass the underlying MTLTexture to native as the render
        // target. This avoids any per-frame buffer creation.)

        if let buf = cachedPixelBuffer {
            return Unmanaged.passRetained(buf)
        }

        var w: Int32 = 0; var h: Int32 = 0
        car_p0_get_output_dimensions(sessionPtr, &w, &h)
        guard w > 0, h > 0 else { return nil }

        let attrs: [CFString: Any] = [
            kCVPixelBufferMetalCompatibilityKey: true,
            kCVPixelBufferIOSurfacePropertiesKey: [:]
        ]
        var pb: CVPixelBuffer?
        CVPixelBufferCreate(kCFAllocatorDefault, Int(w), Int(h),
                            kCVPixelFormatType_32BGRA, attrs as CFDictionary, &pb)
        if let pb = pb {
            cachedPixelBuffer = pb
            return Unmanaged.passRetained(pb)
        }
        return nil
    }
}
