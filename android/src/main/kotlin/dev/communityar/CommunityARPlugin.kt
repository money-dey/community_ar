// CommunityARPlugin.kt
// =============================================================================
// Community AR — Android Flutter plugin
//
// Responsibilities:
//   1. Registered with Flutter as a method channel + texture registry consumer
//   2. Owns the Camera2 session and SurfaceTexture
//   3. Pipes camera frames into the native C++ Session via JNI
//   4. Reads the output texture handle from native and registers it with
//      Flutter's TextureRegistry so the Flutter Texture widget can display it
//
// Threading:
//   - Plugin method-channel calls arrive on the Flutter platform thread
//   - Camera2 device/session callbacks run on CameraStream's camera thread
//   - ALL GL/EGL and native session calls run on GlRenderPipeline's single GL
//     render thread (the JNI lambdas passed to the pipeline are invoked there);
//     see docs/ANDROID_RENDER_PIPELINE.md (Option A: Kotlin owns EGL).
// =============================================================================

package dev.communityar

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.SurfaceTexture
import android.util.Log
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.PluginRegistry
import io.flutter.view.TextureRegistry

class CommunityARPlugin : FlutterPlugin, MethodChannel.MethodCallHandler,
    ActivityAware, PluginRegistry.RequestPermissionsResultListener {
    companion object {
        private const val TAG = "CommunityARPlugin"
        const val CHANNEL = "dev.communityar/methods"
        private const val CAMERA_PERMISSION_REQUEST = 0xCA

        // Portrait display buffer. The landscape camera is sampled into this via
        // the pipeline's per-frame UV transform; see ANDROID_RENDER_PIPELINE.md §4.
        private const val DISPLAY_WIDTH = 720
        private const val DISPLAY_HEIGHT = 1280

        // Max zoom reported when only the digital (GPU-crop) fallback is used.
        private const val DIGITAL_MAX_ZOOM = 5.0

        init { System.loadLibrary("community_ar_native") }
    }

    private lateinit var channel: MethodChannel
    private lateinit var textureRegistry: TextureRegistry
    private lateinit var appContext: Context

    private var surfaceEntry: TextureRegistry.SurfaceTextureEntry? = null
    private var cameraStream: CameraStream? = null
    private var pipeline: GlRenderPipeline? = null
    private var currentLens: String = "front"

    // Activity + a pending permission result (one request in flight at a time).
    private var activity: Activity? = null
    private var pendingPermissionResult: MethodChannel.Result? = null

    // Native handle (Phase0Session*) returned by JNI
    private var nativeSessionPtr: Long = 0

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        appContext = binding.applicationContext
        textureRegistry = binding.textureRegistry
        channel = MethodChannel(binding.binaryMessenger, CHANNEL)
        channel.setMethodCallHandler(this)
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        teardown()
        channel.setMethodCallHandler(null)
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        try {
            when (call.method) {
                "requestCameraPermission" -> requestCameraPermission(result)
                "createSession"    -> result.success(createSession())
                "startCamera"      -> { startCamera(call.argument("lens") ?: "front"); result.success(null) }
                "switchCamera"     -> { switchCamera(call.argument("lens") ?: "front"); result.success(null) }
                "stopCamera"       -> { stopCamera(); result.success(null) }
                "setTestMode"      -> { setTestMode(call.argument("mode") ?: 0); result.success(null) }
                "setZoom"          -> { setZoom(call.argument<Double>("zoom") ?: 1.0); result.success(null) }
                "getMaxZoom"       -> result.success(getMaxZoom())
                "getMinZoom"       -> result.success(getMinZoom())
                "outputTextureId"  -> result.success(surfaceEntry?.id() ?: -1)
                "outputDimensions" -> result.success(getOutputDimensions())
                "getStats"         -> result.success(getStats())
                "dispose"          -> { teardown(); result.success(null) }
                else -> result.notImplemented()
            }
        } catch (e: Throwable) {
            Log.e(TAG, "Method ${call.method} failed", e)
            result.error("CAR_ERROR", e.message, null)
        }
    }

    // -------------------------------------------------------------------------
    // Session
    // -------------------------------------------------------------------------
    private fun createSession(): Long {
        if (nativeSessionPtr != 0L) return nativeSessionPtr

        // Allocate a Flutter SurfaceTexture entry. This gives us a textureId
        // that the Flutter Texture widget will display, and the SurfaceTexture
        // the GL pipeline turns into its EGL window (presentation) surface.
        val entry = textureRegistry.createSurfaceTexture()
        surfaceEntry = entry

        // Stand up the GL/EGL render pipeline (Option A: Kotlin owns EGL). The
        // native ops below are invoked on the pipeline's GL thread, where the
        // EGL context is current — required, since they issue GL calls.
        val gl = GlRenderPipeline(
            displayWidth = DISPLAY_WIDTH,
            displayHeight = DISPLAY_HEIGHT,
            nativeCreateSession = { st -> nativeCreateSession(st) },
            nativeSubmitFrameAr = { ptr, tex, w, h, mat, ts ->
                nativeSubmitFrameAr(ptr, tex.toLong(), w, h, mat, ts)
            },
            nativeDestroySession = { ptr -> nativeDestroySession(ptr) },
        )
        pipeline = gl
        if (!gl.start(entry.surfaceTexture())) {
            Log.e(TAG, "GL pipeline failed to start")
        }
        nativeSessionPtr = gl.nativeSessionPtr
        Log.i(TAG, "Native session created: $nativeSessionPtr")
        return nativeSessionPtr
    }

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------
    private fun startCamera(lens: String) {
        cameraStream?.stop()
        currentLens = lens
        val gl = pipeline ?: run {
            Log.e(TAG, "startCamera before pipeline is ready")
            return
        }
        val target = gl.cameraInputSurface() ?: run {
            Log.e(TAG, "startCamera: camera input surface not available")
            return
        }
        val isFront = lens == "front"
        cameraStream = CameraStream(appContext, isFront, target) { sensorOrientation ->
            gl.setOrientation(sensorOrientation, isFront)
        }
        cameraStream?.start()
        // Reset zoom for the newly opened camera (its range/backend may differ).
        gl.setDigitalZoom(1f)
    }

    // -------------------------------------------------------------------------
    // Zoom — hybrid: hardware (Camera2 CONTROL_ZOOM_RATIO) where the device
    // supports it, else a digital crop in the GL pipeline. One setZoom() surface.
    // -------------------------------------------------------------------------
    private fun setZoom(zoom: Double) {
        val z = zoom.toFloat()
        val cs = cameraStream
        if (cs != null && cs.supportsHardwareZoom) {
            cs.setZoom(z)
            pipeline?.setDigitalZoom(1f)   // keep the GL crop out of the way
        } else {
            pipeline?.setDigitalZoom(z)
        }
    }

    private fun getMaxZoom(): Double {
        val cs = cameraStream
        return if (cs != null && cs.supportsHardwareZoom) cs.maxZoom.toDouble()
               else DIGITAL_MAX_ZOOM
    }

    // Minimum zoom. Below 1.0 only on hardware-zoom devices with an ultra-wide
    // lens (that's the "deeper zoom out"). Digital zoom can't go below 1.0 —
    // there's no image beyond the sensor's full frame — so it floors at 1.0.
    private fun getMinZoom(): Double {
        val cs = cameraStream
        return if (cs != null && cs.supportsHardwareZoom) cs.minZoom.toDouble()
               else 1.0
    }

    private fun switchCamera(lens: String) {
        cameraStream?.stop()
        startCamera(lens)
    }

    // Stop Camera2 but keep the GL pipeline + native session alive (app
    // background). The next startCamera reopens the camera into the same
    // pipeline surface; the Flutter texture id is unchanged.
    private fun stopCamera() {
        cameraStream?.stop()
        cameraStream = null
    }

    // -------------------------------------------------------------------------
    // Diagnostics
    // -------------------------------------------------------------------------
    private fun setTestMode(mode: Int) {
        if (nativeSessionPtr != 0L) nativeSetTestMode(nativeSessionPtr, mode)
    }

    private fun getOutputDimensions(): Map<String, Int> {
        if (nativeSessionPtr == 0L) return mapOf("width" to 0, "height" to 0)
        val dims = IntArray(2)
        nativeGetOutputDimensions(nativeSessionPtr, dims)
        return mapOf("width" to dims[0], "height" to dims[1])
    }

    private fun getStats(): Map<String, Any> {
        if (nativeSessionPtr == 0L) return emptyMap()
        val s = nativeGetStats(nativeSessionPtr)
        return mapOf(
            "avgFrameTimeMs" to s[0],
            "framesProcessed" to s[1].toInt(),
            "framesDropped" to s[2].toInt()
        )
    }

    private fun teardown() {
        cameraStream?.stop()
        cameraStream = null
        // The pipeline destroys the native session on its GL thread (native
        // teardown issues GL deletes that need the context current), then tears
        // down EGL and stops the thread.
        pipeline?.release()
        pipeline = null
        nativeSessionPtr = 0
        surfaceEntry?.release()
        surfaceEntry = null
    }

    // -------------------------------------------------------------------------
    // Runtime camera permission (Android 6+)
    // -------------------------------------------------------------------------
    private fun requestCameraPermission(result: MethodChannel.Result) {
        if (ContextCompat.checkSelfPermission(appContext, Manifest.permission.CAMERA)
                == PackageManager.PERMISSION_GRANTED) {
            result.success(true)
            return
        }
        val act = activity
        if (act == null) {
            Log.e(TAG, "requestCameraPermission: no attached activity")
            result.success(false)
            return
        }
        // One request in flight at a time; resolve any prior pending one as false.
        pendingPermissionResult?.success(false)
        pendingPermissionResult = result
        ActivityCompat.requestPermissions(
            act, arrayOf(Manifest.permission.CAMERA), CAMERA_PERMISSION_REQUEST)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ): Boolean {
        if (requestCode != CAMERA_PERMISSION_REQUEST) return false
        val granted = grantResults.isNotEmpty() &&
            grantResults[0] == PackageManager.PERMISSION_GRANTED
        pendingPermissionResult?.success(granted)
        pendingPermissionResult = null
        return true
    }

    // -------------------------------------------------------------------------
    // ActivityAware — needed to hold an Activity for requestPermissions()
    // -------------------------------------------------------------------------
    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activity = binding.activity
        binding.addRequestPermissionsResultListener(this)
    }

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) {
        onAttachedToActivity(binding)
    }

    override fun onDetachedFromActivityForConfigChanges() {
        activity = null
    }

    override fun onDetachedFromActivity() {
        activity = null
    }

    // -------------------------------------------------------------------------
    // JNI declarations — implemented in jni_bridge.cpp
    // -------------------------------------------------------------------------
    private external fun nativeCreateSession(flutterSurfaceTexture: SurfaceTexture): Long
    private external fun nativeDestroySession(ptr: Long)
    private external fun nativeSubmitFrame(ptr: Long, textureHandle: Long,
                                           width: Int, height: Int,
                                           rotation: Int, isFront: Int)
    private external fun nativeSubmitFrameDisplay(ptr: Long, textureHandle: Long,
                                                  width: Int, height: Int,
                                                  rotation: Int, isFront: Int,
                                                  texMatrix: FloatArray)
    // AR path (WP-B): subsumes nativeSubmitFrameDisplay — identical output when
    // no effect graph is installed. The Phase 0 symbol stays for fallback.
    private external fun nativeSubmitFrameAr(ptr: Long, textureHandle: Long,
                                             width: Int, height: Int,
                                             texMatrix: FloatArray,
                                             timestampNs: Long)
    private external fun nativeSetTestMode(ptr: Long, mode: Int)
    private external fun nativeGetOutputDimensions(ptr: Long, out: IntArray)
    private external fun nativeGetStats(ptr: Long): FloatArray
}
