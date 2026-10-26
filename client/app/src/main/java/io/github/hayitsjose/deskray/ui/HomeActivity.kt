package io.github.hayitsjose.deskray.ui

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import io.github.hayitsjose.deskray.ImmersiveActivity
import io.github.hayitsjose.deskray.net.HostRepository
import io.github.hayitsjose.deskray.net.HostStore
import io.github.hayitsjose.deskray.net.Identity
import io.github.hayitsjose.deskray.net.NvHttp
import io.github.hayitsjose.deskray.net.Pairing
import io.github.hayitsjose.deskray.net.SavedHost
import io.github.hayitsjose.deskray.stream.StreamSettings
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors

/** The launcher panel: paired machines, adding (pairing) new ones, stream settings. */
class HomeActivity : Activity() {
    private val executor = Executors.newCachedThreadPool()
    private val main = Handler(Looper.getMainLooper())
    private lateinit var hosts: HostRepository
    private lateinit var certs: HostStore
    private lateinit var list: LinearLayout
    private val online = ConcurrentHashMap<String, Boolean>()  // uniqueId -> reachable
    private val identity by lazy { Identity.loadOrCreate(filesDir) }  // first call generates a key: off the UI thread
    private var polling = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        hosts = HostRepository(this)
        certs = HostStore(filesDir)
        setContentView(buildLayout())
        intent.getStringExtra(EXTRA_ERROR)?.let { Toast.makeText(this, it, Toast.LENGTH_LONG).show() }
    }

    override fun onResume() {
        super.onResume()
        refreshList()
        polling = true
        poll()
    }

    override fun onPause() {
        polling = false
        super.onPause()
    }

    override fun onDestroy() {
        executor.shutdownNow()
        super.onDestroy()
    }

    // --- layout -------------------------------------------------------------

    private fun buildLayout(): View {
        val pad = dp(28)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }
        root.addView(text("Deskray", 30f, bold = true))
        root.addView(text("Your Linux machines", 15f, secondary = true).apply { setPadding(0, 0, 0, dp(16)) })

        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(ScrollView(this).apply { addView(list) }, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))

        root.addView(Button(this).apply {
            text = "Add machine"
            setOnClickListener { promptAddHost() }
        }, LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT).apply { topMargin = dp(12) })

        root.addView(settingsRow(), LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT).apply { topMargin = dp(16) })
        return root
    }

    private fun refreshList() {
        list.removeAllViews()
        val saved = hosts.all()
        if (saved.isEmpty()) {
            list.addView(text(
                "No machines yet. Tap Add machine and enter its Tailscale name or IP address. " +
                    "The machine needs Sunshine running (host/host-setup.sh).",
                15f, secondary = true,
            ))
            return
        }
        saved.forEach { list.addView(hostRow(it)) }
    }

    private fun hostRow(host: SavedHost): View {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(8), 0, dp(8))
        }
        val state = online[host.uniqueId]
        row.addView(text("●", 20f).apply {
            setTextColor(when (state) { true -> 0xFF4CAF50.toInt(); false -> 0xFF757575.toInt(); null -> 0xFFFFB300.toInt() })
            setPadding(0, 0, dp(12), 0)
        })
        row.addView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(text(host.name, 18f, bold = true))
            addView(text(host.address + if (state == false) " · offline" else "", 13f, secondary = true))
        }, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        row.addView(Button(this).apply {
            text = "Window"
            setOnClickListener {
                startActivity(Intent(this@HomeActivity, WindowStreamActivity::class.java)
                    .putExtra(WindowStreamActivity.EXTRA_HOST, host.address)
                    .putExtra(WindowStreamActivity.EXTRA_NAME, host.name))
            }
        })
        row.addView(Button(this).apply {
            text = "Immersive"
            setOnClickListener {
                ImmersiveActivity.launch(this@HomeActivity, host.address)
                finishAndRemoveTask()  // hybrid apps run one mode at a time
            }
        })
        row.addView(Button(this).apply {
            text = "Remove"
            setOnClickListener { confirmRemove(host) }
        })
        return row
    }

    private fun settingsRow(): View {
        val s = StreamSettings.load(this)
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        fun save(update: (StreamSettings) -> StreamSettings) = update(StreamSettings.load(this)).save(this)

        row.addView(text("Resolution ", 14f, secondary = true))
        row.addView(spinner(StreamSettings.RESOLUTIONS.map { "${it.first}×${it.second}" },
            StreamSettings.RESOLUTIONS.indexOf(s.width to s.height)) { i ->
            val (w, h) = StreamSettings.RESOLUTIONS[i]
            save { it.copy(width = w, height = h) }
        })
        row.addView(text("   Frame rate ", 14f, secondary = true))
        row.addView(spinner(StreamSettings.FRAME_RATES.map { "$it fps" }, StreamSettings.FRAME_RATES.indexOf(s.fps)) { i ->
            save { it.copy(fps = StreamSettings.FRAME_RATES[i]) }
        })
        row.addView(text("   Bitrate ", 14f, secondary = true))
        row.addView(spinner(StreamSettings.BITRATES_KBPS.map { "${it / 1000} Mbps" },
            StreamSettings.BITRATES_KBPS.indexOf(s.bitrateKbps)) { i ->
            save { it.copy(bitrateKbps = StreamSettings.BITRATES_KBPS[i]) }
        })
        return row
    }

    // --- adding and removing machines ----------------------------------------

    private fun promptAddHost() {
        val input = EditText(this).apply {
            hint = "my-desktop or 100.64.0.10"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setSingleLine()
        }
        AlertDialog.Builder(this)
            .setTitle("Add machine")
            .setMessage("Tailscale name or IP address of a machine running Sunshine.")
            .setView(LinearLayout(this).apply { setPadding(dp(24), 0, dp(24), 0); addView(input, MATCH_PARENT, WRAP_CONTENT) })
            .setPositiveButton("Connect") { _, _ -> input.text.toString().trim().takeIf { it.isNotEmpty() }?.let(::addHost) }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun addHost(address: String) {
        val progress = AlertDialog.Builder(this).setMessage("Contacting $address…").setCancelable(false).show()
        executor.execute {
            try {
                val http = NvHttp(address, identity)
                val info = http.serverInfoHttp()
                val hostId = info.require("uniqueid")
                val name = info["hostname"] ?: address

                // Already paired (a pinned cert the host still accepts)? Just list it.
                http.serverCert = certs.pinnedCert(hostId)
                val paired = http.serverCert != null &&
                    runCatching { http.serverInfoHttps()["PairStatus"] == "1" }.getOrDefault(false)
                if (paired) {
                    hosts.save(SavedHost(name, address, hostId))
                    main.post { progress.dismiss(); online[hostId] = true; refreshList() }
                    return@execute
                }

                val pin = Pairing.generatePin()
                main.post { progress.dismiss(); showPin(pin, name) }
                val pairing = Pairing(http, identity)
                val result = pairing.pair(info, pin)
                main.post {
                    pinDialog?.dismiss()
                    if (result == Pairing.Result.PAIRED) {
                        certs.pin(hostId, pairing.serverCert!!)
                        hosts.save(SavedHost(name, address, hostId))
                        online[hostId] = true
                        refreshList()
                        toast("Paired with $name")
                    } else {
                        toast(when (result) {
                            Pairing.Result.PIN_WRONG -> "Wrong PIN. Try adding $name again."
                            Pairing.Result.ALREADY_IN_PROGRESS -> "$name is pairing with another device."
                            else -> "Pairing with $name failed."
                        })
                    }
                }
            } catch (e: Exception) {
                main.post { progress.dismiss(); pinDialog?.dismiss(); toast("Couldn't reach $address: ${e.message}") }
            }
        }
    }

    private var pinDialog: AlertDialog? = null

    private fun showPin(pin: String, hostName: String) {
        val body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(dp(24), dp(8), dp(24), dp(8))
            addView(text(pin, 56f, bold = true).apply { letterSpacing = 0.3f; gravity = Gravity.CENTER })
            addView(text(
                "On $hostName, open https://localhost:47990/pin and enter this PIN. " +
                    "This window closes by itself once it's accepted.",
                15f, secondary = true,
            ).apply { gravity = Gravity.CENTER })
        }
        pinDialog = AlertDialog.Builder(this)
            .setTitle("Pair with $hostName")
            .setView(body)
            .setNegativeButton("Cancel", null)  // the background request just ends unused
            .show()
    }

    private fun confirmRemove(host: SavedHost) {
        AlertDialog.Builder(this)
            .setTitle("Remove ${host.name}?")
            .setMessage("You'll need to pair again with a PIN to use it.")
            .setPositiveButton("Remove") { _, _ ->
                hosts.remove(host.uniqueId)
                certs.forget(host.uniqueId)
                refreshList()
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    // --- online status ---------------------------------------------------------

    private fun poll() {
        if (!polling) return
        hosts.all().forEach { host ->
            executor.execute {
                val up = runCatching { NvHttp(host.address, identity).serverInfoHttp() }.isSuccess
                if (online.put(host.uniqueId, up) != up) main.post { if (polling) refreshList() }
            }
        }
        main.postDelayed(::poll, 5_000)
    }

    // --- small view helpers ----------------------------------------------------

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun text(s: String, sizeSp: Float, bold: Boolean = false, secondary: Boolean = false) =
        TextView(this).apply {
            text = s
            textSize = sizeSp
            if (bold) setTypeface(typeface, Typeface.BOLD)
            if (secondary) alpha = 0.7f
        }

    private fun spinner(items: List<String>, selected: Int, onPick: (Int) -> Unit) = Spinner(this).apply {
        adapter = ArrayAdapter(this@HomeActivity, android.R.layout.simple_spinner_dropdown_item, items)
        setSelection(selected.coerceAtLeast(0), false)
        onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, position: Int, id: Long) = onPick(position)
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
    }

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_LONG).show()

    companion object {
        const val EXTRA_ERROR = "error"
    }
}
