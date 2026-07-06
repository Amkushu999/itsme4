package com.itsme.amkush.hooks

import com.itsme.amkush.utils.Logger

/**
 * NativeFrameProducer — JNI bridge to the native Ashmem ring-buffer + UDS pipeline.
 *
 * When [com.itsme.amkush.AppState.useNativeHook] is true the Java-level
 * Camera1/2/X hooks are bypassed entirely and the Zygisk module intercepts
 * processCaptureResult in cameraserver directly.  This object supplies the
 * synthetic frames by:
 *
 *   1. Creating an Ashmem ring buffer (NV12, 1280×720, 3 slots).
 *   2. Decoding the source video / RTSP / image via FFmpeg into that buffer.
 *   3. Sending the Ashmem fd to the cameraserver hook via an abstract-namespace
 *      Unix Domain Socket using SCM_RIGHTS.
 *
 * Lifecycle:
 *   Call [start] when the user starts injection; call [stop] when they stop.
 */
object NativeFrameProducer {

    private const val TAG = "NativeFrameProducer"

    private var libraryLoaded = false

    init {
        try {
            System.loadLibrary("frame_producer")
            libraryLoaded = true
            Logger.i(Logger.HOOK, "$TAG native library loaded")
        } catch (e: UnsatisfiedLinkError) {
            Logger.e("$TAG Failed to load frame_producer native library: ${e.message}")
        }
    }

    /**
     * Start the frame pipeline for the given source path.
     *
     * @param sourcePath Absolute path to a video file, or an RTSP URL.
     * @return true on success, false on failure.
     */
    fun start(sourcePath: String): Boolean {
        if (!libraryLoaded) {
            Logger.e("$TAG start() skipped — native library not loaded")
            return false
        }
        return try {
            val rc = nativeStart(sourcePath)
            if (rc == 0) {
                Logger.i(Logger.HOOK, "$TAG started: $sourcePath")
                true
            } else {
                Logger.e("$TAG nativeStart returned $rc")
                false
            }
        } catch (e: Throwable) {
            Logger.e("$TAG start() threw: ${e.message}")
            false
        }
    }

    /**
     * Stop the decode thread and release the Ashmem ring buffer.
     * Safe to call multiple times.
     */
    fun stop() {
        if (!libraryLoaded) return
        try {
            nativeStop()
            Logger.i(Logger.HOOK, "$TAG stopped")
        } catch (e: Throwable) {
            Logger.e("$TAG stop() threw: ${e.message}")
        }
    }

    private external fun nativeStart(sourcePath: String): Int
    private external fun nativeStop()
}
