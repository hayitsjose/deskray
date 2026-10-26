package io.github.hayitsjose.deskray

import android.app.NativeActivity
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.util.Log
import io.github.hayitsjose.deskray.stream.NativeBridge
import io.github.hayitsjose.deskray.stream.StreamSession
import io.github.hayitsjose.deskray.stream.StreamSettings
import io.github.hayitsjose.deskray.ui.HomeActivity

/**
 * The curved-screen mode. NativeActivity runs the OpenXR loop in C++; this
 * subclass starts the stream and handles the way back to the Home panel.
 *
 * From the Home panel: [launch]. For development without the UI:
 *   adb shell am start -n io.github.hayitsjose.deskray/.ImmersiveActivity --es host <address>
 */
class ImmersiveActivity : NativeActivity(), StreamSession.Listener {
    private var session: StreamSession? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        val settings = StreamSettings.load(this)
        // Before super.onCreate: the XR thread sizes its video surface from this.
        NativeBridge.setStreamSize(settings.width, settings.height)
        super.onCreate(savedInstanceState)

        val host = intent.getStringExtra(EXTRA_HOST)
        if (host == null) {
            Log.i(TAG, "no host given; returning to the machine list")
            exitToPanel(null)
            return
        }
        session = StreamSession(this, host, settings, this).also { it.start() }
    }

    override fun onStatus(message: String) {
        Log.i(TAG, message)
    }

    override fun onFailed(message: String) {
        runOnUiThread { exitToPanel(message) }
    }

    /** Called from the XR thread (main.cpp) when B or Y is pressed. */
    @Suppress("unused")
    fun onMenuButton() {
        runOnUiThread { exitToPanel(null) }
    }

    /** `am start ... --ez stop true` ends the stream from adb. */
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (intent.getBooleanExtra("stop", false)) exitToPanel(null)
    }

    override fun onDestroy() {
        session?.stop()
        session = null
        super.onDestroy()
    }

    /** Hybrid-app hand-off: open Home with a pending intent that shows our panel. */
    private fun exitToPanel(error: String?) {
        session?.stop()
        session = null
        val panel = Intent(this, HomeActivity::class.java).apply {
            if (error != null) putExtra(HomeActivity.EXTRA_ERROR, error)
        }
        val pending = PendingIntent.getActivity(
            this, 0, panel, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        startActivity(
            Intent(Intent.ACTION_MAIN)
                .addCategory(Intent.CATEGORY_HOME)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                .putExtra("extra_launch_in_home_pending_intent", pending),
        )
        finishAndRemoveTask()
    }

    companion object {
        private const val TAG = "deskray"
        const val EXTRA_HOST = "host"

        /** Leave the panel for the curved screen, streaming [address]. */
        fun launch(context: Context, address: String) {
            context.startActivity(
                Intent(context, ImmersiveActivity::class.java)
                    .setAction(Intent.ACTION_MAIN)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    .putExtra(EXTRA_HOST, address),
            )
        }
    }
}
