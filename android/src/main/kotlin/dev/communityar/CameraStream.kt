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
                    val req = device.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                        addTarget(targetSurface)
                        set(CaptureRequest.CONTROL_AF_MODE,
                            CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE)
                    }.build()
                    session.setRepeatingRequest(req, null, cameraHandler)
                }
                override fun onConfigureFailed(session: CameraCaptureSession) {
                    Log.e(TAG, "Capture session config failed")
                }
            }, cameraHandler)
    }
}
