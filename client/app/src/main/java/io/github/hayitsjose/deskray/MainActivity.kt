package io.github.hayitsjose.deskray

import android.app.NativeActivity
import android.os.Bundle
import android.util.Log

/**
 * NativeActivity runs the OpenXR loop in C++; this subclass only hosts the
 * Kotlin control plane (pairing, launch) next to it.
 */
class MainActivity : NativeActivity() {
    private var session: DevSession? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = getSharedPreferences("dev", MODE_PRIVATE)
        val host = intent.getStringExtra("host")?.also { prefs.edit().putString("host", it).apply() }
            ?: prefs.getString("host", null)
        if (host == null) {
            Log.i("deskray", "no host set; launch with --es host <address>")
            return
        }
        session = DevSession(this, host).also { it.start() }
    }

    /** `am start ... --ez stop true` quits the stream on the host, then the app (singleTask). */
    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        if (intent.getBooleanExtra("stop", false)) {
            Log.i("deskray", "stop requested")
            session?.stop()
            session = null
            finish()
        }
    }

    override fun onDestroy() {
        session?.stop()
        super.onDestroy()
    }
}
