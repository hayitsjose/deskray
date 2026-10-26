package io.github.hayitsjose.deskray.stream

import android.view.Surface

/** JNI entry points into libdeskray's moonlight-common-c glue (stream/stream_jni.cpp). */
object NativeBridge {
    init {
        // NativeActivity dlopens the library itself, but JNI symbol lookup only
        // sees libraries loaded through this class loader.
        System.loadLibrary("deskray")
    }

    /** Extra query parameters moonlight-common-c wants on /launch and /resume. */
    external fun launchQueryParameters(): String

    /**
     * Blocks through the RTSP handshake; returns 0 once the stream is running
     * (it then continues on moonlight-common-c's own threads), or an error code.
     */
    external fun startStream(
        address: String,
        appVersion: String,
        gfeVersion: String?,
        rtspSessionUrl: String?,
        serverCodecModeSupport: Int,
        width: Int,
        height: Int,
        fps: Int,
        bitrateKbps: Int,
        packetSize: Int,
        streamingRemotely: Int,
        riKey: ByteArray,
        riKeyId: Int,
    ): Int

    external fun stopStream()
    external fun isConnected(): Boolean

    /** Size of the immersive video surface; set before the XR session starts. */
    external fun setStreamSize(width: Int, height: Int)

    /** Windowed mode: decode into this surface (null detaches it). */
    external fun setVideoSurface(surface: Surface?)

    // Windowed-mode input. Positions are in stream pixels.
    external fun sendMousePosition(x: Int, y: Int, width: Int, height: Int)
    external fun sendMouseButton(down: Boolean, button: Int)
    external fun sendScroll(vertical: Int, horizontal: Int)

    /** Android KeyEvent fields; returns true if the key went to the host. */
    external fun sendKey(keyCode: Int, action: Int, metaState: Int, repeatCount: Int): Boolean

    // moonlight-common-c button ids.
    const val BUTTON_LEFT = 1
    const val BUTTON_MIDDLE = 2
    const val BUTTON_RIGHT = 3
    const val BUTTON_X1 = 4
    const val BUTTON_X2 = 5
}
