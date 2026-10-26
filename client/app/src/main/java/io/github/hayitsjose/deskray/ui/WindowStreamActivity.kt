package io.github.hayitsjose.deskray.ui

import android.app.Activity
import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.widget.FrameLayout
import android.widget.TextView
import io.github.hayitsjose.deskray.stream.NativeBridge
import io.github.hayitsjose.deskray.stream.StreamSession
import io.github.hayitsjose.deskray.stream.StreamSettings
import kotlin.math.roundToInt

/**
 * The desktop in an ordinary Horizon OS window, next to the browser and other
 * panel apps. MediaCodec decodes into a SurfaceView; the system pointer
 * (controller ray, hands, touchpad) arrives as MotionEvents mapped to absolute
 * stream coordinates, and keys go through the same table as immersive mode.
 */
class WindowStreamActivity : Activity(), SurfaceHolder.Callback, StreamSession.Listener {
    private lateinit var settings: StreamSettings
    private lateinit var video: AspectSurfaceView
    private lateinit var status: TextView
    private var session: StreamSession? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = StreamSettings.load(this)
        title = intent.getStringExtra(EXTRA_NAME) ?: "Deskray"

        video = AspectSurfaceView(this, settings.width.toFloat() / settings.height).apply {
            holder.addCallback(this@WindowStreamActivity)
            setOnTouchListener { v, e -> onPointer(v, e) }
            setOnGenericMotionListener { v, e -> onPointer(v, e) }
            isFocusable = true
            isFocusableInTouchMode = true
        }
        status = TextView(this).apply {
            textSize = 18f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
            text = "Connecting…"
        }
        setContentView(FrameLayout(this).apply {
            setBackgroundColor(Color.BLACK)
            addView(video, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT, Gravity.CENTER))
            addView(status, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT))
        })
    }

    // The decoder needs the surface before the stream's video setup runs.
    override fun surfaceCreated(holder: SurfaceHolder) {
        NativeBridge.setVideoSurface(holder.surface)
        if (session == null) {
            val host = intent.getStringExtra(EXTRA_HOST) ?: return finish()
            session = StreamSession(this, host, settings, this).also { it.start() }
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        NativeBridge.setVideoSurface(null)
    }

    // One mode at a time: leaving the window ends the stream on the host too.
    override fun onStop() {
        session?.stop()
        session = null
        super.onStop()
        finish()
    }

    override fun onStatus(message: String) = runOnUiThread { status.text = message }

    override fun onStarted() = runOnUiThread {
        status.visibility = View.GONE
        video.requestFocus()
    }

    override fun onFailed(message: String) = runOnUiThread {
        status.visibility = View.VISIBLE
        status.text = message
    }

    // --- input -----------------------------------------------------------------

    private fun onPointer(v: View, e: MotionEvent): Boolean {
        if (!NativeBridge.isConnected() || v.width == 0 || v.height == 0) return false
        val w = settings.width
        val h = settings.height
        val x = (e.x / v.width * w).roundToInt().coerceIn(0, w - 1)
        val y = (e.y / v.height * h).roundToInt().coerceIn(0, h - 1)
        // Mice report buttons explicitly; touch-like pointers (controller ray,
        // hands) only send down/up, which mean the primary button.
        val mouse = e.isFromSource(InputDevice.SOURCE_MOUSE)

        when (e.actionMasked) {
            MotionEvent.ACTION_HOVER_ENTER, MotionEvent.ACTION_HOVER_MOVE, MotionEvent.ACTION_MOVE ->
                NativeBridge.sendMousePosition(x, y, w, h)
            MotionEvent.ACTION_DOWN -> {
                NativeBridge.sendMousePosition(x, y, w, h)
                if (!mouse) NativeBridge.sendMouseButton(true, NativeBridge.BUTTON_LEFT)
                v.requestFocus()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                NativeBridge.sendMousePosition(x, y, w, h)
                if (!mouse) NativeBridge.sendMouseButton(false, NativeBridge.BUTTON_LEFT)
            }
            MotionEvent.ACTION_BUTTON_PRESS -> mouseButton(e.actionButton)?.let { NativeBridge.sendMouseButton(true, it) }
            MotionEvent.ACTION_BUTTON_RELEASE -> mouseButton(e.actionButton)?.let { NativeBridge.sendMouseButton(false, it) }
            MotionEvent.ACTION_SCROLL -> NativeBridge.sendScroll(
                (e.getAxisValue(MotionEvent.AXIS_VSCROLL) * 120).roundToInt(),
                (e.getAxisValue(MotionEvent.AXIS_HSCROLL) * 120).roundToInt(),
            )
            else -> return false
        }
        return true
    }

    private fun mouseButton(androidButton: Int) = when (androidButton) {
        MotionEvent.BUTTON_PRIMARY -> NativeBridge.BUTTON_LEFT
        MotionEvent.BUTTON_SECONDARY -> NativeBridge.BUTTON_RIGHT
        MotionEvent.BUTTON_TERTIARY -> NativeBridge.BUTTON_MIDDLE
        MotionEvent.BUTTON_BACK -> NativeBridge.BUTTON_X1
        MotionEvent.BUTTON_FORWARD -> NativeBridge.BUTTON_X2
        else -> null
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean =
        NativeBridge.sendKey(event.keyCode, event.action, event.metaState, event.repeatCount) ||
            super.dispatchKeyEvent(event)

    companion object {
        const val EXTRA_HOST = "host"
        const val EXTRA_NAME = "name"
    }
}

/** A SurfaceView that keeps the stream's aspect ratio inside a freely resized window. */
class AspectSurfaceView(context: Context, private val aspect: Float) : SurfaceView(context) {
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        val h = MeasureSpec.getSize(heightMeasureSpec)
        if (w == 0 || h == 0) return super.onMeasure(widthMeasureSpec, heightMeasureSpec)
        if (w / aspect <= h) setMeasuredDimension(w, (w / aspect).roundToInt())
        else setMeasuredDimension((h * aspect).roundToInt(), h)
    }
}
