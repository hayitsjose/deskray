package io.github.hayitsjose.deskray.stream

import android.content.Context
import android.util.Log
import io.github.hayitsjose.deskray.net.HostStore
import io.github.hayitsjose.deskray.net.Identity
import io.github.hayitsjose.deskray.net.NvHttp
import io.github.hayitsjose.deskray.net.toHex
import java.security.SecureRandom

/**
 * One stream of a paired host's "Desktop" app: launch (or resume) it on the
 * host, then hand the session to moonlight-common-c. Pairing happens earlier,
 * from the machine list. Listener calls arrive on the session thread.
 */
class StreamSession(
    private val context: Context,
    private val address: String,
    private val settings: StreamSettings,
    private val listener: Listener,
) {
    interface Listener {
        fun onStatus(message: String) {}
        fun onStarted() {}
        fun onFailed(message: String)
    }

    @Volatile private var http: NvHttp? = null
    @Volatile private var streaming = false
    @Volatile private var stopped = false

    fun start() {
        Thread({
            try {
                connect()
            } catch (e: Exception) {
                Log.e(TAG, "session failed: ${e.message}", e)
                if (!stopped) listener.onFailed(e.message ?: e.javaClass.simpleName)
            }
        }, "deskray-session").start()
    }

    fun stop() {
        stopped = true
        if (streaming) {
            NativeBridge.stopStream()
            streaming = false
        }
        // Quit on the host so Sunshine runs its undo prep-cmds (Wi-Fi power save).
        http?.let { h -> Thread { runCatching { h.https("cancel", "") } }.start() }
    }

    private fun connect() {
        listener.onStatus("Connecting to $address…")
        val identity = Identity.loadOrCreate(context.filesDir)
        val http = NvHttp(address, identity).also { this.http = it }

        val hostId = http.serverInfoHttp().require("uniqueid")
        http.serverCert = HostStore(context.filesDir).pinnedCert(hostId)
            ?: throw IllegalStateException("Not paired with this machine. Add it again from the machine list.")
        val info = http.serverInfoHttps()
        if (info["PairStatus"] != "1") {
            throw IllegalStateException("The machine no longer recognizes this headset. Remove it and add it again.")
        }

        val apps = http.https("applist", "").apps
        val app = apps.firstOrNull { it["AppTitle"] == "Desktop" } ?: apps.firstOrNull()
            ?: throw IllegalStateException("Sunshine has no apps configured")
        val appId = app.getValue("ID").toInt()

        val random = SecureRandom()
        val riKey = ByteArray(16).also(random::nextBytes)
        val riKeyId = random.nextInt()
        val running = info["currentgame"]?.toIntOrNull() ?: 0
        val verb = if (running == appId) "resume" else "launch"
        if (running != 0 && running != appId) http.https("cancel", "")
        if (stopped) return

        listener.onStatus("Starting the desktop on ${info["hostname"] ?: address}…")
        val s = settings
        val launch = http.https(
            verb,
            "appid=$appId&mode=${s.width}x${s.height}x${s.fps}&additionalStates=1&sops=0" +
                "&rikey=${riKey.toHex()}&rikeyid=$riKeyId&localAudioPlayMode=0" +
                "&surroundAudioInfo=$STEREO_AUDIO_INFO&remoteControllersBitmap=0&gcmap=0&gcpersist=0" +
                NativeBridge.launchQueryParameters(),
            NvHttp.NO_READ_TIMEOUT,
        )
        val ok = if (verb == "launch") launch["gamesession"] != "0" else launch["resume"] != "0"
        if (!ok) throw IllegalStateException("Sunshine refused to $verb the desktop")
        if (stopped) return

        val onTailnet = address.startsWith("100.") || address.endsWith(".ts.net")
        val ret = NativeBridge.startStream(
            address, info.require("appversion"), info["GfeVersion"], launch["sessionUrl0"],
            info["ServerCodecModeSupport"]?.toIntOrNull() ?: 0,
            s.width, s.height, s.fps, s.bitrateKbps,
            if (onTailnet) 1024 else 1392,
            if (onTailnet) 1 else 0,
            riKey, riKeyId,
        )
        if (ret != 0) throw IllegalStateException("Stream connection failed (error $ret)")
        streaming = true
        if (stopped) {
            stop()
            return
        }
        Log.i(TAG, "stream running: ${s.width}x${s.height}@${s.fps}, ${s.bitrateKbps} kbps")
        listener.onStarted()
    }

    private companion object {
        const val TAG = "deskray"
        const val STEREO_AUDIO_INFO = (0x3 shl 16) or 2  // channelMask << 16 | channelCount
    }
}
