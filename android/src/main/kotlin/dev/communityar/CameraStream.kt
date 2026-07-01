// CameraStream.kt
// =============================================================================
// Camera2 wrapper that delivers camera frames as OES external GL textures.
//
// Architecture:
//   1. Allocate a SurfaceTexture (OES external) — this is the GL texture
//      the camera will write into directly, with zero CPU copy
//   2. Open Camera2 and configure it to output to this SurfaceTexture
//   3. On each frame, SurfaceTexture.OnFrameAvailableListener fires; we
//      updateTexImage() (releases the camera frame to the texture) and
//      invoke the callback with the texture name
//   4. The callback passes the texture name to native code which renders it
//
// This entire pipeline keeps pixel data on the GPU at all times.
// =============================================================================

package dev.communityar

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.SurfaceTexture
import android.hardware.camera2.*
import android.opengl.*
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.util.Size
import android.view.Surface
import androidx.core.content.ContextCompat

class CameraStream(
    private val context: Context,
    private val isFront: Boolean,
    private val onFrame: (textureName: Int, width: Int, height: Int, rotation: Int) -> Unit,
) {
    companion object {
        private const val TAG = "CommunityARCameraStream"
        private const val TARGET_WIDTH = 1280
        private const val TARGET_HEIGHT = 720
    }

    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var surfaceTexture: SurfaceTexture? = null
    private var surface: Surface? = null
    private var cameraThread: HandlerThread? = null
    private var cameraHandler: Handler? = null

    // OES texture name. Allocated by us using GLES, written to by the camera.
    private var oesTextureName: Int = 0

    private var cameraRotation: Int = 0

    fun start() {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA)
            != PackageManager.PERMISSION_GRANTED) {
            Log.e(TAG, "CAMERA permission not granted")
            return
        }

        cameraThread = HandlerThread("CommunityAR-Camera").apply { start() }
        cameraHandler = Handler(cameraThread!!.looper)

        // Create OES texture using EGL/GLES (must be done on a thread with a
        // current GL context; the host plugin should set one up first).
        oesTextureName = createOesTexture()
        surfaceTexture = SurfaceTexture(oesTextureName).apply {
            setDefaultBufferSize(TARGET_WIDTH, TARGET_HEIGHT)
            setOnFrameAvailableListener({ st ->
                try {
                    st.updateTexImage()
                    onFrame(oesTextureName, TARGET_WIDTH, TARGET_HEIGHT, cameraRotation)
                } catch (e: Throwable) {
                    Log.e(TAG, "Frame delivery failed", e)
                }
            }, cameraHandler)
        }
        surface = Surface(surfaceTexture)

        openCamera()
    }

    fun stop() {
        captureSession?.close()
        captureSession = null
        cameraDevice?.close()
        cameraDevice = null
        surface?.release()
        surface = null
        surfaceTexture?.release()
        surfaceTexture = null
        if (oesTextureName != 0) {
            val tex = IntArray(1) { oesTextureName }
            GLES20.glDeleteTextures(1, tex, 0)
            oesTextureName = 0
        }
        cameraThread?.quitSafely()
        cameraThread?.join()
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
        cameraRotation = chars[CameraCharacteristics.SENSOR_ORIENTATION] ?: 0

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
        val targetSurface = surface ?: return
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

    // -------------------------------------------------------------------------
    // GL helpers
    // -------------------------------------------------------------------------
    private fun createOesTexture(): Int {
        val tex = IntArray(1)
        GLES20.glGenTextures(1, tex, 0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, tex[0])
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        return tex[0]
    }
}
