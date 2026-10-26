package io.github.hayitsjose.deskray

import android.content.Context
import android.util.Log
import io.github.hayitsjose.deskray.net.HostStore
import io.github.hayitsjose.deskray.net.Identity
import io.github.hayitsjose.deskray.net.NvHttp
import io.github.hayitsjose.deskray.net.NvResponse
import io.github.hayitsjose.deskray.net.Pairing
import io.github.hayitsjose.deskray.net.toHex
import io.github.hayitsjose.deskray.stream.NativeBridge
import java.security.SecureRandom

/**
 * Phase 3 harness: connect to one host given on the command line, pairing if
 * needed, and start a stream of its "Desktop" app. Everything is reported in
 * logcat until the in-VR UI (Phase 7) replaces this.
 *
 *   adb shell am start -n io.github.hayitsjose.deskray/.MainActivity --es host <address>
 */
class DevSession(private val context: Context, private val address: String) {
    private var thread: Thread? = null
    @Volatile private var http: NvHttp? = null
    @Volatile private var streaming = false

    fun start() {
        thread = Thread({ run() }, "deskray-session").apply { start() }
    }

    fun stop() {
        if (streaming) {
            NativeBridge.stopStream()
            streaming = false
        }
        // Quit on the host so Sunshine runs its undo prep-cmds (Wi-Fi power save).
        http?.let { h -> Thread { runCatching { h.https("cancel", "") } }.start() }
    }

    private fun run() {
        try {
            connect()
        } catch (e: Exception) {
            Log.e(TAG, "session failed: ${e.message}", e)
        }
    }

    private fun connect() {
        val identity = Identity.loadOrCreate(context.filesDir)
        val hosts = HostStore(context.filesDir)
        val http = NvHttp(address, identity).also { this.http = it }
        Log.i(TAG, "client uniqueid ${identity.uniqueId}; contacting $address")

        var info = http.serverInfoHttp()
        val hostId = info.require("uniqueid")
        val hostName = info["hostname"] ?: address
        Log.i(TAG, "host $hostName (${info["appversion"]}), state ${info["state"]}, paired=${info["PairStatus"]}")

        // Plain HTTP always reports PairStatus 0: only a mutual-TLS request proves
        // who we are. So with a pinned cert, ask over HTTPS before re-pairing.
        http.serverCert = hosts.pinnedCert(hostId)
        val httpsInfo = http.serverCert?.let {
            runCatching { http.serverInfoHttps() }
                .onFailure { e -> Log.w(TAG, "HTTPS check with pinned cert failed: ${e.message}") }
                .getOrNull()
        }
        if (httpsInfo?.get("PairStatus") == "1") {
            info = httpsInfo
            Log.i(TAG, "already paired with $hostName")
        } else {
            if (!pair(http, identity, hosts, hostId, hostName, info)) return
            info = http.serverInfoHttps()
        }
        val apps = http.https("applist", "").apps
        Log.i(TAG, "apps: ${apps.joinToString { "${it["AppTitle"]}(${it["ID"]})" }}")
        val app = apps.firstOrNull { it["AppTitle"] == "Desktop" } ?: apps.firstOrNull()
            ?: throw IllegalStateException("host has no apps")
        val appId = app.getValue("ID").toInt()

        val onTailnet = address.startsWith("100.") || address.endsWith(".ts.net")
        val random = SecureRandom()
        val riKey = ByteArray(16).also(random::nextBytes)
        val riKeyId = random.nextInt()

        val running = info["currentgame"]?.toIntOrNull() ?: 0
        val verb = if (running == appId) "resume" else "launch"
        if (running != 0 && running != appId) http.https("cancel", "")
        val launch = http.https(
            verb,
            "appid=$appId&mode=${WIDTH}x${HEIGHT}x$FPS&additionalStates=1&sops=0" +
                "&rikey=${riKey.toHex()}&rikeyid=$riKeyId&localAudioPlayMode=0" +
                "&surroundAudioInfo=$STEREO_AUDIO_INFO&remoteControllersBitmap=0&gcmap=0&gcpersist=0" +
                NativeBridge.launchQueryParameters(),
            NvHttp.NO_READ_TIMEOUT,
        )
        val ok = if (verb == "launch") launch["gamesession"] != "0" else launch["resume"] != "0"
        if (!ok) throw IllegalStateException("host refused to $verb app $appId")
        Log.i(TAG, "$verb ok, session ${launch["sessionUrl0"]}")

        val ret = NativeBridge.startStream(
            address, info.require("appversion"), info["GfeVersion"], launch["sessionUrl0"],
            info["ServerCodecModeSupport"]?.toIntOrNull() ?: 0,
            WIDTH, HEIGHT, FPS, BITRATE_KBPS,
            if (onTailnet) 1024 else 1392,
            if (onTailnet) 1 else 0,
            riKey, riKeyId,
        )
        streaming = ret == 0
        Log.i(TAG, if (streaming) "stream running" else "startStream failed: $ret")
    }

    private fun pair(
        http: NvHttp, identity: Identity, hosts: HostStore,
        hostId: String, hostName: String, info: NvResponse,
    ): Boolean {
        val pin = Pairing.generatePin()
        Log.i(TAG, "PAIRING PIN $pin: open https://localhost:47990/pin on $hostName and enter it")
        val pairing = Pairing(http, identity)
        val result = pairing.pair(info, pin)
        Log.i(TAG, "pairing result: $result")
        if (result != Pairing.Result.PAIRED) {
            hosts.forget(hostId)
            return false
        }
        hosts.pin(hostId, pairing.serverCert!!)
        http.serverCert = pairing.serverCert
        return true
    }

    companion object {
        private const val TAG = "deskray"
        // Matches the laptop panel for now; Phase 4 picks this per host.
        private const val WIDTH = 1920
        private const val HEIGHT = 1080
        private const val FPS = 60
        private const val BITRATE_KBPS = 20_000
        private const val STEREO_AUDIO_INFO = (0x3 shl 16) or 2  // channelMask << 16 | channelCount
    }
}
