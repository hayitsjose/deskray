package io.github.hayitsjose.deskray.stream

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
}
