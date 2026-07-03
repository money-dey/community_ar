// GlRenderPipeline.kt
// =============================================================================
// The Android GL/EGL render pipeline — "Option A" from
// docs/ANDROID_RENDER_PIPELINE.md: one render thread owns one EGL context; the
// camera OES texture and an EGL window surface (built from Flutter's
// SurfaceTexture) both live on it; each frame renders and eglSwapBuffers
// presents into the Flutter texture.
//
// Why this class exists:
//   The original bring-up sketch had *no* EGL context anywhere — GL calls ran
//   on the camera thread with no current context (no-ops), and native rendered
//   into an offscreen FBO that was never presented into the Flutter texture, so
//   the preview was always black. This class builds the missing "context +
//   threading + texture-sharing + presentation" model in one place.
//
// Threading contract:
//   Everything GL — EGL setup, the camera OES texture, native session creation,
//   the per-frame draw, and teardown — happens on the single "CommunityAR-GL"
//   HandlerThread. The camera OES texture is created on the SAME context native
//   renders on, so its raw GL name is valid native-side (fixing the old
//   cross-context handle bug). Camera2's own state callbacks run on a separate
//   camera thread (see CameraStream); only frame *delivery*
//   (onFrameAvailable → updateTexImage → draw → swap) lands on this GL thread.
//
// NOT runtime-verified: there is no Android device / GPU runtime in the dev
// environment, so this compiles and links but the on-device verification
// checklist in ANDROID_RENDER_PIPELINE.md §5 still has to be walked on hardware.
// The orientation math in computeUvTransform in particular needs on-device
// tuning (see its comment).
// =============================================================================

package dev.communityar

import android.graphics.SurfaceTexture
import android.opengl.*
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.Surface
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class GlRenderPipeline(
    // Display (window-surface) size. The camera image is sampled into this via
    // the per-frame UV transform, so this is the shape the Flutter Texture takes.
    private val displayWidth: Int,
    private val displayHeight: Int,
    // Native ops. ALL of these are invoked on the GL render thread with the EGL
    // context current — nativeCreateSession/destroy issue GL calls (VAO/VBO/
    // shader create+delete) that require it.
    private val nativeCreateSession: (SurfaceTexture) -> Long,
    private val nativeSubmitFrameDisplay:
        (ptr: Long, tex: Int, w: Int, h: Int, rotation: Int, isFront: Int,
         texMatrix: FloatArray) -> Unit,
    private val nativeDestroySession: (ptr: Long) -> Unit,
) {
    companion object {
        private const val TAG = "CommunityARGlPipeline"
        // Camera capture resolution (landscape sensor space).
        private const val CAMERA_WIDTH = 1280
        private const val CAMERA_HEIGHT = 720

        // --- Orientation tuning (see docs/ANDROID_RENDER_PIPELINE.md §4) ---
        // Rotation, in degrees, applied to bring the landscape camera image
        // upright in our portrait display buffer. Because the camera is 1280×720
        // and the buffer is 720×1280, a correct 90°/270° here also removes the
        // "stretched" look (the axis swap makes 1280↔1280 / 720↔720). The exact
        // value is device/convention-dependent and can only be confirmed on
        // hardware. On-device the front sensor is 270°, and setting 270 here
        // reproduced the old sensor-derived result exactly ("chin-left", a
        // quarter-turn off) — so the correct value is a 90° neighbour: 0 (try
        // first), else 180 (if it comes out upside-down); 90/270 give the
        // sideways-the-other-way case. Same value works for both cameras; front
        // adds a horizontal mirror (MIRROR_FRONT).
        private const val UV_ROTATION_DEG = 0
        // Front camera horizontal mirror. A mirrored selfie preview is the
        // usual convention, but on-device it was flagged as an unwanted
        // horizontal flip, so it's off. Set true to restore the mirror-image
        // (selfie) view.
        private const val MIRROR_FRONT = false
    }

    private var glThread: HandlerThread? = null
    private var glHandler: Handler? = null

    private var eglDisplay: EGLDisplay = EGL14.EGL_NO_DISPLAY
    private var eglContext: EGLContext = EGL14.EGL_NO_CONTEXT
    private var eglConfig: EGLConfig? = null
    private var windowSurface: EGLSurface = EGL14.EGL_NO_SURFACE
    private var flutterSurface: Surface? = null

    // Camera input: an external-OES texture the camera writes into.
    private var cameraTexId: Int = 0
    private var cameraSurfaceTexture: SurfaceTexture? = null
    private var cameraSurface: Surface? = null

    // Native Phase0Session*, created on the GL thread once the context is live.
    @Volatile var nativeSessionPtr: Long = 0L
        private set

    // Orientation state, updated by the camera layer, read per frame.
    @Volatile private var cameraRotation: Int = 0
    @Volatile private var isFront: Boolean = true

    // Digital-zoom factor (>=1). Used only as the fallback when the camera has
    // no hardware zoom; the plugin holds it at 1.0 when hardware zoom is active.
    // Applied as a crop-toward-centre in the UV transform.
    @Volatile private var digitalZoom: Float = 1f

    // Per-frame scratch matrices (only ever touched on the GL thread).
    private val stMatrix = FloatArray(16)
    private val orientMatrix = FloatArray(16)
    private val uvMatrix = FloatArray(16)

    // One-shot diagnostic so the on-device orientation tuning has real data.
    private var loggedFirstFrame = false

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * Spins up the GL thread, brings up EGL against [flutterSurfaceTexture],
     * creates the native session and the camera OES texture. Blocks the caller
     * (a one-time, few-ms setup on the platform thread) until done so callers
     * can immediately grab [cameraInputSurface]. Returns true on success.
     */
    fun start(flutterSurfaceTexture: SurfaceTexture): Boolean {
        val thread = HandlerThread("CommunityAR-GL").apply { start() }
        glThread = thread
        val handler = Handler(thread.looper)
        glHandler = handler

        val latch = CountDownLatch(1)
        var ok = false
        handler.post {
            try {
                initEgl(flutterSurfaceTexture)
                nativeSessionPtr = nativeCreateSession(flutterSurfaceTexture)
                createCameraTexture()
                ok = nativeSessionPtr != 0L && eglContext != EGL14.EGL_NO_CONTEXT
                Log.i(TAG, "GL pipeline init ok=$ok session=$nativeSessionPtr")
            } catch (e: Throwable) {
                Log.e(TAG, "GL pipeline init failed", e)
            } finally {
                latch.countDown()
            }
        }
        latch.await(5, TimeUnit.SECONDS)
        return ok
    }

    /** Surface the camera should render into (backed by the OES texture). */
    fun cameraInputSurface(): Surface? = cameraSurface

    /** Update orientation used to build the per-frame UV transform. */
    fun setOrientation(rotationDegrees: Int, front: Boolean) {
        cameraRotation = rotationDegrees
        isFront = front
    }

    /** Digital-zoom fallback factor (>=1). 1.0 = no zoom. */
    fun setDigitalZoom(zoom: Float) {
        digitalZoom = zoom.coerceIn(1f, 8f)
    }

    /** Tears everything down on the GL thread, then stops the thread. */
    fun release() {
        val handler = glHandler
        if (handler != null) {
            val latch = CountDownLatch(1)
            handler.post {
                try {
                    teardownGl()
                } catch (e: Throwable) {
                    Log.e(TAG, "GL teardown failed", e)
                } finally {
                    latch.countDown()
                }
            }
            latch.await(2, TimeUnit.SECONDS)
        }
        glThread?.quitSafely()
        try { glThread?.join(1000) } catch (_: InterruptedException) {}
        glThread = null
        glHandler = null
    }

    // -------------------------------------------------------------------------
    // EGL / GL setup (GL thread)
    // -------------------------------------------------------------------------
    private fun initEgl(flutterSurfaceTexture: SurfaceTexture) {
        eglDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        if (eglDisplay == EGL14.EGL_NO_DISPLAY) error("eglGetDisplay failed")
        val ver = IntArray(2)
        if (!EGL14.eglInitialize(eglDisplay, ver, 0, ver, 1)) error("eglInitialize failed")

        val configAttrs = intArrayOf(
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            EGL14.EGL_SURFACE_TYPE, EGL14.EGL_WINDOW_BIT,
            EGL14.EGL_RED_SIZE, 8,
            EGL14.EGL_GREEN_SIZE, 8,
            EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_ALPHA_SIZE, 8,
            EGL14.EGL_NONE,
        )
        val configs = arrayOfNulls<EGLConfig>(1)
        val numConfigs = IntArray(1)
        if (!EGL14.eglChooseConfig(eglDisplay, configAttrs, 0, configs, 0, 1, numConfigs, 0)
            || numConfigs[0] == 0) {
            error("eglChooseConfig failed")
        }
        eglConfig = configs[0]

        // ES 3.x context (matches the #version 300 es shaders native compiles).
        val ctxAttrs = intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE)
        eglContext = EGL14.eglCreateContext(
            eglDisplay, eglConfig, EGL14.EGL_NO_CONTEXT, ctxAttrs, 0)
        if (eglContext == EGL14.EGL_NO_CONTEXT) error("eglCreateContext failed")

        // The Flutter SurfaceTexture becomes our presentation target.
        flutterSurfaceTexture.setDefaultBufferSize(displayWidth, displayHeight)
        flutterSurface = Surface(flutterSurfaceTexture)
        windowSurface = EGL14.eglCreateWindowSurface(
            eglDisplay, eglConfig, flutterSurface, intArrayOf(EGL14.EGL_NONE), 0)
        if (windowSurface == EGL14.EGL_NO_SURFACE) error("eglCreateWindowSurface failed")

        if (!EGL14.eglMakeCurrent(eglDisplay, windowSurface, windowSurface, eglContext)) {
            error("eglMakeCurrent failed: 0x${Integer.toHexString(EGL14.eglGetError())}")
        }
    }

    private fun createCameraTexture() {
        val tex = IntArray(1)
        GLES20.glGenTextures(1, tex, 0)
        cameraTexId = tex[0]
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, cameraTexId)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)

        val st = SurfaceTexture(cameraTexId)
        st.setDefaultBufferSize(CAMERA_WIDTH, CAMERA_HEIGHT)
        // Frame delivery lands on the GL thread so updateTexImage + the draw
        // happen where the context is current.
        st.setOnFrameAvailableListener({ onFrameAvailable() }, glHandler)
        cameraSurfaceTexture = st
        cameraSurface = Surface(st)
    }

    // -------------------------------------------------------------------------
    // Per-frame render (GL thread)
    // -------------------------------------------------------------------------
    private fun onFrameAvailable() {
        val st = cameraSurfaceTexture ?: return
        val ptr = nativeSessionPtr
        if (ptr == 0L) return
        try {
            st.updateTexImage()
            st.getTransformMatrix(stMatrix)
            if (!loggedFirstFrame) {
                loggedFirstFrame = true
                Log.i(TAG, "first frame: sensorOrientation=$cameraRotation " +
                    "front=$isFront uvRotation=$UV_ROTATION_DEG " +
                    "st=[${stMatrix.joinToString(",")}]")
            }
            val m = computeUvTransform(stMatrix, isFront)
            nativeSubmitFrameDisplay(
                ptr, cameraTexId, displayWidth, displayHeight,
                cameraRotation, if (isFront) 1 else 0, m)
            // Present into the Flutter texture; its raster thread composites it.
            EGL14.eglSwapBuffers(eglDisplay, windowSurface)
        } catch (e: Throwable) {
            Log.e(TAG, "onFrameAvailable failed", e)
        }
    }

    /**
     * Build the UV transform that samples the landscape camera into our portrait
     * buffer: a fixed [UV_ROTATION_DEG] rotation about the UV centre (upright the
     * image + remove the stretch via the axis swap), an optional horizontal
     * mirror for the front camera, and finally the SurfaceTexture transform
     * (`st`, which handles the OES crop/flip). The shader applies uTexMatrix to
     * the quad UVs, so the composed result is `st * orient`.
     *
     * The rotation is a single fixed value (not derived from SENSOR_ORIENTATION)
     * because on-device testing showed both cameras need the same quarter-turn;
     * only the mirror differs. Reuses [uvMatrix]; the value is copied out
     * immediately by the (synchronous) native call, so reuse is safe.
     *
     * NOTE: [UV_ROTATION_DEG] / [MIRROR_FRONT] are the on-device tuning knobs
     * (ANDROID_RENDER_PIPELINE.md §4). This assumes a portrait-held device;
     * following live device rotation is a separate follow-up (needs the display
     * rotation + swapping the buffer dimensions in landscape).
     */
    private fun computeUvTransform(st: FloatArray, front: Boolean): FloatArray {
        Matrix.setIdentityM(orientMatrix, 0)
        Matrix.translateM(orientMatrix, 0, 0.5f, 0.5f, 0f)
        // Digital zoom: sample a smaller centred region (scale UVs by 1/zoom
        // about the centre). Uniform, so it commutes with the rotation below.
        // No-op when zoom == 1 (e.g. whenever hardware zoom is in use).
        if (digitalZoom != 1f) {
            val s = 1f / digitalZoom
            Matrix.scaleM(orientMatrix, 0, s, s, 1f)
        }
        Matrix.rotateM(orientMatrix, 0, UV_ROTATION_DEG.toFloat(), 0f, 0f, 1f)
        if (front && MIRROR_FRONT) Matrix.scaleM(orientMatrix, 0, -1f, 1f, 1f)
        Matrix.translateM(orientMatrix, 0, -0.5f, -0.5f, 0f)
        Matrix.multiplyMM(uvMatrix, 0, st, 0, orientMatrix, 0)
        return uvMatrix
    }

    // -------------------------------------------------------------------------
    // Teardown (GL thread)
    // -------------------------------------------------------------------------
    private fun teardownGl() {
        if (nativeSessionPtr != 0L) {
            nativeDestroySession(nativeSessionPtr)
            nativeSessionPtr = 0L
        }
        cameraSurface?.release()
        cameraSurface = null
        cameraSurfaceTexture?.release()
        cameraSurfaceTexture = null
        if (cameraTexId != 0) {
            GLES20.glDeleteTextures(1, intArrayOf(cameraTexId), 0)
            cameraTexId = 0
        }
        if (eglDisplay != EGL14.EGL_NO_DISPLAY) {
            EGL14.eglMakeCurrent(eglDisplay, EGL14.EGL_NO_SURFACE,
                EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
            if (windowSurface != EGL14.EGL_NO_SURFACE) {
                EGL14.eglDestroySurface(eglDisplay, windowSurface)
                windowSurface = EGL14.EGL_NO_SURFACE
            }
            if (eglContext != EGL14.EGL_NO_CONTEXT) {
                EGL14.eglDestroyContext(eglDisplay, eglContext)
                eglContext = EGL14.EGL_NO_CONTEXT
            }
            EGL14.eglTerminate(eglDisplay)
            eglDisplay = EGL14.EGL_NO_DISPLAY
        }
        flutterSurface?.release()
        flutterSurface = null
    }
}
