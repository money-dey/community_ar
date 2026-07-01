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
//   - Camera frames arrive on the camera background thread
//   - All native calls are non-blocking; native runs its own render thread
// =============================================================================

package dev.communityar

import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.camera2.*
import android.opengl.*
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.view.TextureRegistry

class CommunityARPlugin : FlutterPlugin, MethodChannel.MethodCallHandler {
    companion object {
        private const val TAG = "CommunityARPlugin"
        const val CHANNEL = "dev.communityar/methods"

        init { System.loadLibrary("community_ar_native") }
    }

    private lateinit var channel: MethodChannel
    private lateinit var textureRegistry: TextureRegistry
    private lateinit var appContext: Context

    private var surfaceEntry: TextureRegistry.SurfaceTextureEntry? = null
    private var cameraStream: CameraStream? = null

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
                "createSession"    -> result.success(createSession())
                "startCamera"      -> { startCamera(call.argument("lens") ?: "front"); result.success(null) }
                "switchCamera"     -> { switchCamera(call.argument("lens") ?: "front"); result.success(null) }
                "setTestMode"      -> { setTestMode(call.argument("mode") ?: 0); result.success(null) }
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
        // that the Flutter Texture widget will display, and a SurfaceTexture
        // that we'll let the native side render into.
        val entry = textureRegistry.createSurfaceTexture()
        surfaceEntry = entry

        // Hand the EGL context to native. The native side will set up its own
        // GLES context shared with Flutter's, allowing zero-copy texture
        // sharing through EGLImage.
        nativeSessionPtr = nativeCreateSession(entry.surfaceTexture())
        Log.i(TAG, "Native session created: $nativeSessionPtr")
        return nativeSessionPtr
    }

    // -------------------------------------------------------------------------
    // Camera
    // -------------------------------------------------------------------------
    private fun startCamera(lens: String) {
        if (cameraStream != null) cameraStream?.stop()
        cameraStream = CameraStream(appContext, isFront = lens == "front") { tex, w, h, rot ->
            // tex is the OES external GL texture name backed by Camera2's SurfaceTexture
            nativeSubmitFrame(nativeSessionPtr, tex.toLong(), w, h, rot, if (lens == "front") 1 else 0)
        }
        cameraStream?.start()
    }

    private fun switchCamera(lens: String) {
        cameraStream?.stop()
        startCamera(lens)
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
        if (nativeSessionPtr != 0L) {
            nativeDestroySession(nativeSessionPtr)
            nativeSessionPtr = 0
        }
        surfaceEntry?.release()
        surfaceEntry = null
    }

    // -------------------------------------------------------------------------
    // JNI declarations — implemented in jni_bridge.cpp
    // -------------------------------------------------------------------------
    private external fun nativeCreateSession(flutterSurfaceTexture: SurfaceTexture): Long
    private external fun nativeDestroySession(ptr: Long)
    private external fun nativeSubmitFrame(ptr: Long, textureHandle: Long,
                                           width: Int, height: Int,
                                           rotation: Int, isFront: Int)
    private external fun nativeSetTestMode(ptr: Long, mode: Int)
    private external fun nativeGetOutputDimensions(ptr: Long, out: IntArray)
    private external fun nativeGetStats(ptr: Long): FloatArray
}
