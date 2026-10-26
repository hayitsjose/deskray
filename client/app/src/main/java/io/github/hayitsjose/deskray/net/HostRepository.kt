package io.github.hayitsjose.deskray.net

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

/** A paired machine as the user sees it: a name, how to reach it, and its stable id. */
data class SavedHost(val name: String, val address: String, val uniqueId: String)

/** The machine list. Pinned certificates live separately in [HostStore]. */
class HostRepository(context: Context) {
    private val prefs = context.getSharedPreferences("hosts", Context.MODE_PRIVATE)

    fun all(): List<SavedHost> {
        val array = JSONArray(prefs.getString(KEY, "[]"))
        return (0 until array.length()).map { i ->
            val o = array.getJSONObject(i)
            SavedHost(o.getString("name"), o.getString("address"), o.getString("uniqueId"))
        }
    }

    /** Adds the host, or updates its name/address if this machine is already listed. */
    fun save(host: SavedHost) = write(all().filter { it.uniqueId != host.uniqueId } + host)

    fun remove(uniqueId: String) = write(all().filter { it.uniqueId != uniqueId })

    private fun write(hosts: List<SavedHost>) {
        val array = JSONArray()
        hosts.forEach {
            array.put(JSONObject().put("name", it.name).put("address", it.address).put("uniqueId", it.uniqueId))
        }
        prefs.edit().putString(KEY, array.toString()).apply()
    }

    private companion object {
        const val KEY = "list"
    }
}
