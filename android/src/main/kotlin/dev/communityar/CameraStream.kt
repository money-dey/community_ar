// CameraStream.kt
// =============================================================================
// Camera2 wrapper. Opens the requested lens and points a repeating preview
// request at a caller-provided Surface.
//
// Under "Option A" (docs/ANDROID_RENDER_PIPELINE.md) the camera's OES texture,
// its SurfaceTexture, and all GL live in GlRenderPipeline on the GL render
// thread. This class no longer creates any GL objects — it only drives Camera2
// and reports the sensor orientation. That keeps every GL call on the one
// thread that owns the EGL context (the old code created the OES texture here
// with no current context, which was a no-op and part of why the preview was
// black).
//
// Camera2 device/session state callbacks run on this class's own camera thread;
// frame *delivery* is handled by the SurfaceTexture.OnFrameAvailableListener
// that GlRenderPipeline registers on the GL thread.
// =============================================================================

package dev.communityar

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.camera2.*
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.Surface
import androidx.core.content.ContextCompat

class CameraStream(
    private val context: Context,
    private val isFront: Boolean,
    private val targetSurface: Surface,
    // Reports SENSOR_ORIENTATION once the camera is chosen, so the render layer
    // can build the correct UV transform.
    private val onSensorOrientation: (Int) -> Unit,
) {
    companion object {
        private const val TAG = "CommunityARCameraStream"
    }

    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var cameraThread: HandlerThread? = null
    private var cameraHandler: Handler? = null

    // Preview request builder is kept so hardware zoom (CONTROL_ZOOM_RATIO) can
    // be updated and re-submitted without rebuilding the whole request.
    private var previewRequestBuilder: CaptureRequest.Builder? = null

    // Hardware-zoom capability, resolved from CameraCharacteristics when the
    // camera is chosen (readable synchronously after start()). CONTROL_ZOOM_RATIO
    // needs API 30+; older devices fall back to digital zoom in the GL pipeline.
    var supportsHardwareZoom: Boolean = false
        private set
    var maxZoom: Float = 1f
        private set
    // Minimum hardware zoom ratio. On devices with an ultra-wide lens this is
    // BELOW 1.0 (e.g. ~0.5–0.6), which is what lets the preview zoom out wider
    // than the default field of view.
    var minZoom: Float = 1f
        private set
    private var pendingZoom: Float = 1f

    fun start() {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED) {
            Log.e(TAG, "CAMERA permission not granted")
            return
        }
        cameraThread = HandlerThread("CommunityAR-Camera").apply { start() }
        cameraHandler = Handler(cameraThread!!.looper)
        openCamera()
    }

    fun stop() {
        captureSession?.close()
        captureSession = null
        cameraDevice?.close()
        cameraDevice = null
        cameraThread?.quitSafely()
        try { cameraThread?.join(1000) } catch (_: InterruptedException) {}
        cameraThread = null
        cameraHandler = null
    }

    // -------------------------------------------------------------------------
    // Camera lifecycle
    // -------------------------------------------------------------------------
    private fun openCamera() {
        val manager = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
        val cameraId = manager.cameraIdList.firstOrNull { id ->
            val chars = manager.getCameraCharacteristics(id)
            val facing = chars[CameraCharacteristics.LENS_FACING]
            (isFront && facing == CameraCharacteristics.LENS_FACING_FRONT) ||
            (!isFront && facing == CameraCharacteristics.LENS_FACING_BACK)
        } ?: run {
            Log.e(TAG, "No matching camera found")
            return
        }

        val chars = manager.getCameraCharacteristics(cameraId)
        onSensorOrientation(chars[CameraCharacteristics.SENSOR_ORIENTATION] ?: 0)
        resolveZoomRange(chars)

        try {
            manager.openCamera(cameraId, object : CameraDevice.StateCallback() {
                override fun onOpened(device: CameraDevice) {
                    cameraDevice = device
                    createCaptureSession()
                }
                override fun onDisconnected(device: CameraDevice) { device.close() }
                override fun onError(device: CameraDevice, error: Int) {
                    Log.e(TAG, "Camera error: $error"); device.close()
                }
            }, cameraHandler)
        } catch (e: SecurityException) {
            Log.e(TAG, "openCamera permission denied", e)
        }
    }

    private fun createCaptureSession() {
        val device = cameraDevice ?: return
        device.createCaptureSession(
            listOf(targetSurface),
            object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(session: CameraCaptureSession) {
                    captureSession = session
                    val builder = device.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                        addTarget(targetSurface)
                        set(CaptureRequest.CONTROL_AF_MODE,
                            CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE)
                        // Apply any zoom requested before the session existed.
                        if (supportsHardwareZoom &&
                            Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                            set(CaptureRequest.CONTROL_ZOOM_RATIO, pendingZoom)
                        }
                    }
                    previewRequestBuilder = builder
                    session.setRepeatingRequest(builder.build(), null, cameraHandler)
                }
                override fun onConfigureFailed(session: CameraCaptureSession) {
                    Log.e(TAG, "Capture session config failed")
                }
            }, cameraHandler)
    }

    // -------------------------------------------------------------------------
    // Zoom (hardware / CONTROL_ZOOM_RATIO, API 30+)
    // -------------------------------------------------------------------------
    private fun resolveZoomRange(chars: CameraCharacteristics) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val range = chars.get(CameraCharacteristics.CONTROL_ZOOM_RATIO_RANGE)
            if (range != null && range.upper > 1f) {
                supportsHardwareZoom = true
                minZoom = range.lower
                maxZoom = range.upper
                return
            }
        }
        supportsHardwareZoom = false
        minZoom = 1f
        maxZoom = 1f
    }

    /** Set the hardware zoom ratio (clamped to the device range). No-op if the
     *  camera has no hardware zoom — the caller uses digital zoom instead. */
    fun setZoom(ratio: Float) {
        if (!supportsHardwareZoom ||
            Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
        pendingZoom = ratio.coerceIn(minZoom, maxZoom)
        val session = captureSession ?: return
        val builder = previewRequestBuilder ?: return
        builder.set(CaptureRequest.CONTROL_ZOOM_RATIO, pendingZoom)
        try {
            session.setRepeatingRequest(builder.build(), null, cameraHandler)
        } catch (e: Exception) {
            Log.e(TAG, "setZoom failed", e)
        }
    }
}
