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

    // Effect graph pushed before the session existed — applied in
    // createSession() (see setEffectGraph).
    private var pendingGraphTypeIds: IntArray? = null
    private var pendingGraphConfigs: Array<ByteArray>? = null

    // Same pre-session race for the WP-E debug controls.
    private var pendingDebugOverlay: Int? = null
    private var pendingForcePerception: IntArray? = null

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
                // Effect graph (Phase 2 — AR_INTEGRATION_SPEC.md WP-C). The
                // native side queues the swap onto the GL render thread, so
                // calling from the platform thread here is safe.
                "setEffectGraph"   -> {
                    val st = setEffectGraph(call)
                    if (st == 0) result.success(null)
                    else result.error("CAR_ERROR",
                        "setEffectGraph failed (status=$st)", null)
                }
                "clearEffectGraph" -> {
                    pendingGraphTypeIds = null
                    pendingGraphConfigs = null
                    if (nativeSessionPtr != 0L) nativeClearEffectGraph(nativeSessionPtr)
                    result.success(null)
                }
                "getEffectCount"   -> result.success(
                    if (nativeSessionPtr != 0L) nativeGetEffectCount(nativeSessionPtr) else 0)
                // Perception stats (Phase 1 — WP-E first slice). Feeds the
                // example app's HUD; facesDetected is the key bring-up signal.
                "getPerceptionStats" -> {
                    if (nativeSessionPtr == 0L) { result.success(null) } else {
                        val v = nativeGetPerceptionStats(nativeSessionPtr)
                        if (v == null || v.size < 8) { result.success(null) } else {
                            result.success(mapOf(
                                "facesDetected"       to v[0].toInt(),
                                "faceMeshInferenceMs" to v[1].toDouble(),
                                "irisInferenceMs"     to v[2].toDouble(),
                                "hairSegInferenceMs"  to v[3].toDouble(),
                                "pnpSolveMs"          to v[4].toDouble(),
                                "activeFilterCount"   to v[5].toInt(),
                                "skinBaselineLuma"    to v[6].toDouble(),
                                "skinToneValid"       to (v[7] != 0f),
                            ))
                        }
                    }
                }
                // WP-E debug controls. Each is stashed when it arrives before
                // the session exists (same race as setEffectGraph — the Dart
                // side may fire during initState) and applied in createSession.
                "setDebugOverlay" -> {
                    val mode = (call.argument<Number>("mode"))?.toInt() ?: 0
                    if (nativeSessionPtr != 0L) {
                        nativeSetDebugOverlay(nativeSessionPtr, mode)
                    } else {
                        pendingDebugOverlay = mode
                    }
                    result.success(null)
                }
                "forcePerception" -> {
                    fun need(k: String) =
                        (call.argument<Number>(k))?.toInt() ?: 0
                    val req = intArrayOf(
                        need("needFaceLandmarks"), need("needIris"),
                        need("needHair"), need("needSelfieSeg"),
                        need("needPose"), need("needSkinTone"))
                    if (nativeSessionPtr != 0L) {
                        nativeForcePerception(nativeSessionPtr, req[0], req[1],
                            req[2], req[3], req[4], req[5])
                    } else {
                        pendingForcePerception = req
                    }
                    result.success(null)
                }
                "setOneEuroParams" -> {
                    val minCutoff = (call.argument<Number>("minCutoff"))?.toFloat() ?: 1.0f
                    val beta      = (call.argument<Number>("beta"))?.toFloat() ?: 0.007f
                    val dCutoff   = (call.argument<Number>("dCutoff"))?.toFloat() ?: 1.0f
                    if (nativeSessionPtr != 0L) {
                        nativeSetOneEuroParams(nativeSessionPtr, minCutoff, beta, dCutoff)
                    }
                    result.success(null)
                }
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

        // Hand native the on-device model path so perception can load models
        // (extracted from bundled assets on first launch). Must happen before
        // the first frame that runs perception; the native side copies the
        // string and reads it when the neural backend is lazily created.
        if (nativeSessionPtr != 0L) {
            extractModelsIfNeeded()?.let { dir ->
                nativeSetModelDirectory(nativeSessionPtr, dir)
                Log.i(TAG, "Model directory: $dir")
            }
            // Apply an effect graph that arrived before the session existed
            // (the Dart widget installs its initial effects during initState,
            // racing session creation — previously that first install was
            // silently dropped, so the app's default effects never activated).
            pendingGraphTypeIds?.let { ids ->
                pendingGraphConfigs?.let { cfgs ->
                    val st = nativeSetEffectGraph(nativeSessionPtr, ids, cfgs)
                    Log.i(TAG, "Applied pre-session effect graph " +
                        "(${ids.size} effects, status=$st)")
                }
            }
            pendingGraphTypeIds = null
            pendingGraphConfigs = null

            // Debug controls that raced session creation (same pattern).
            pendingDebugOverlay?.let { nativeSetDebugOverlay(nativeSessionPtr, it) }
            pendingDebugOverlay = null
            pendingForcePerception?.let { r ->
                nativeForcePerception(nativeSessionPtr,
                    r[0], r[1], r[2], r[3], r[4], r[5])
            }
            pendingForcePerception = null
        }
        return nativeSessionPtr
    }

    // -------------------------------------------------------------------------
    // Models: the MediaPipe .tflite files are bundled as plugin assets (see
    // android/build.gradle.kts sourceSets → native/models) and extracted to
    // filesDir/models on first launch — TFLite loads from a real file path
    // (see the "Model file paths differ between platforms" gotcha, CLAUDE.md).
    // Idempotent: existing non-empty files are kept. Returns the directory, or
    // null when no models are bundled (perception then stays inactive).
    // -------------------------------------------------------------------------
    private fun extractModelsIfNeeded(): String? {
        return try {
            val outDir = java.io.File(appContext.filesDir, "models")
            if (!outDir.exists()) outDir.mkdirs()
            val names = appContext.assets.list("")
                ?.filter { it.endsWith(".tflite") } ?: emptyList()
            if (names.isEmpty()) {
                Log.w(TAG, "No .tflite models bundled — perception inactive " +
                    "(run tools/fetch_models.sh before building)")
                return null
            }
            for (name in names) {
                val outFile = java.io.File(outDir, name)
                if (outFile.exists() && outFile.length() > 0L) continue
                appContext.assets.open(name).use { input ->
                    outFile.outputStream().use { output -> input.copyTo(output) }
                }
            }
            outDir.absolutePath
        } catch (e: Throwable) {
            Log.e(TAG, "Model extraction failed", e)
            null
        }
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

    // -------------------------------------------------------------------------
    // Effect graph — unpack the Dart-serialized parallel arrays (typeIds +
    // per-effect config byte blobs) and hand them to native. Returns the
    // CARStatus (0 = OK; 2 = invalid session; 3 = invalid argument).
    // -------------------------------------------------------------------------
    private fun setEffectGraph(call: MethodCall): Int {
        val typeIds = call.argument<List<Int>>("typeIds") ?: return 3
        val configs = call.argument<List<ByteArray>>("configs") ?: return 3
        if (typeIds.isEmpty() || typeIds.size != configs.size) return 3
        if (nativeSessionPtr == 0L) {
            // Session not created yet (Dart pushes its initial graph during
            // initState, racing createSession). Stash it; createSession applies
            // it, so the app's default effects actually activate.
            pendingGraphTypeIds = typeIds.toIntArray()
            pendingGraphConfigs = configs.toTypedArray()
            return 0
        }
        return nativeSetEffectGraph(
            nativeSessionPtr, typeIds.toIntArray(), configs.toTypedArray())
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
    private external fun nativeSetModelDirectory(ptr: Long, dir: String)
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
    // Effect graph (Phase 2 C ABI). All return CARStatus/count as Int.
    private external fun nativeSetEffectGraph(ptr: Long, typeIds: IntArray,
                                              configs: Array<ByteArray>): Int
    private external fun nativeClearEffectGraph(ptr: Long): Int
    private external fun nativeGetEffectCount(ptr: Long): Int
    private external fun nativeGetPerceptionStats(ptr: Long): FloatArray?
    // WP-E debug controls (Phase 1 C ABI).
    private external fun nativeSetDebugOverlay(ptr: Long, modeMask: Int)
    private external fun nativeForcePerception(ptr: Long,
                                               faceLandmarks: Int, iris: Int,
                                               hair: Int, selfieSeg: Int,
                                               pose: Int, skinTone: Int)
    private external fun nativeSetOneEuroParams(ptr: Long, minCutoff: Float,
                                                beta: Float, dCutoff: Float)
    private external fun nativeSetTestMode(ptr: Long, mode: Int)
    private external fun nativeGetOutputDimensions(ptr: Long, out: IntArray)
    private external fun nativeGetStats(ptr: Long): FloatArray
}
