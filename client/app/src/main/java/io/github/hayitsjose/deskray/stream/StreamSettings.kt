package io.github.hayitsjose.deskray.stream

import android.content.Context

data class StreamSettings(val width: Int, val height: Int, val fps: Int, val bitrateKbps: Int) {
    companion object {
        val RESOLUTIONS = listOf(1280 to 720, 1920 to 1080, 2560 to 1440)
        val FRAME_RATES = listOf(60, 90)
        val BITRATES_KBPS = listOf(10_000, 20_000, 40_000, 60_000)

        private const val PREFS = "stream"

        fun load(context: Context): StreamSettings {
            val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            return StreamSettings(
                p.getInt("width", 1920), p.getInt("height", 1080),
                p.getInt("fps", 60), p.getInt("bitrate", 20_000),
            )
        }
    }

    fun save(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putInt("width", width).putInt("height", height)
            .putInt("fps", fps).putInt("bitrate", bitrateKbps)
            .apply()
    }
}
