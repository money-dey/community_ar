// CameraStream.swift
// =============================================================================
// AVFoundation camera wrapper producing zero-copy MTLTextures via
// CVMetalTextureCache. Each frame goes:
//
//   AVCaptureVideoDataOutput → CVPixelBuffer (IOSurface-backed, BGRA8)
//     → CVMetalTextureRef (via CVMetalTextureCacheCreateTextureFromImage)
//     → MTLTexture (via CVMetalTextureGetTexture)
//
// No copies, GPU receives the frame directly.
// =============================================================================

import AVFoundation
import Metal
import CoreVideo

class CameraStream: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    private let isFront: Bool
    private let onFrame: (MTLTexture, Int, Int, Int) -> Void

    private let session = AVCaptureSession()
    private let queue = DispatchQueue(label: "dev.communityar.camera")
    private var device: MTLDevice!
    private var textureCache: CVMetalTextureCache?

    init(isFront: Bool, onFrame: @escaping (MTLTexture, Int, Int, Int) -> Void) {
        self.isFront = isFront
        self.onFrame = onFrame
    }

    func start() {
        guard AVCaptureDevice.authorizationStatus(for: .video) == .authorized else {
            AVCaptureDevice.requestAccess(for: .video) { [weak self] granted in
                if granted { self?.start() }
            }
            return
        }

        device = MTLCreateSystemDefaultDevice()
        var cache: CVMetalTextureCache?
        CVMetalTextureCacheCreate(kCFAllocatorDefault, nil, device, nil, &cache)
        textureCache = cache

        session.beginConfiguration()
        session.sessionPreset = .hd1280x720

        let position: AVCaptureDevice.Position = isFront ? .front : .back
        guard let cameraDevice = AVCaptureDevice.default(.builtInWideAngleCamera,
                                                          for: .video, position: position),
              let input = try? AVCaptureDeviceInput(device: cameraDevice),
              session.canAddInput(input) else {
            session.commitConfiguration(); return
        }
        session.addInput(input)

        let output = AVCaptureVideoDataOutput()
        output.videoSettings = [kCVPixelBufferPixelFormatTypeKey as String:
                                 kCVPixelFormatType_32BGRA]
        output.alwaysDiscardsLateVideoFrames = true
        output.setSampleBufferDelegate(self, queue: queue)
        if session.canAddOutput(output) { session.addOutput(output) }

        // Orientation: portrait for front-facing default
        if let connection = output.connection(with: .video) {
            connection.videoOrientation = .portrait
            connection.isVideoMirrored = isFront
        }

        session.commitConfiguration()
        session.startRunning()
    }

    func stop() {
        session.stopRunning()
        for input in session.inputs { session.removeInput(input) }
        for output in session.outputs { session.removeOutput(output) }
        textureCache = nil
    }

    func captureOutput(_ output: AVCaptureOutput,
                       didOutput sampleBuffer: CMSampleBuffer,
                       from connection: AVCaptureConnection) {
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer),
              let cache = textureCache else { return }

        let width  = CVPixelBufferGetWidth(pixelBuffer)
        let height = CVPixelBufferGetHeight(pixelBuffer)

        var cvTexture: CVMetalTexture?
        let result = CVMetalTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault, cache, pixelBuffer, nil,
            .bgra8Unorm, width, height, 0, &cvTexture)
        guard result == kCVReturnSuccess,
              let cvTex = cvTexture,
              let metalTex = CVMetalTextureGetTexture(cvTex) else { return }

        // For front camera at portrait, AVFoundation has already applied
        // mirroring and rotation when isVideoMirrored / videoOrientation are
        // set, so we report rotation = 0.
        onFrame(metalTex, width, height, 0)
    }
}
