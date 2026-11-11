package br.com.truckbox.app.diagnostics

import org.json.JSONArray
import org.json.JSONObject

/** Device/truck scope is checked before merging. Older pages cannot revive an inactive code. */
object DiagnosticMerge {
    private data class Point(val boot: Long?, val uptime: Long?, val unix: Long?)
    private fun point(value: Any?): Point = when (value) {
        is JSONArray -> Point(if (value.isNull(0)) null else value.optLong(0), if (value.isNull(1)) null else value.optLong(1), if (value.isNull(2)) null else value.optLong(2))
        is JSONObject -> Point(if (value.isNull("boot")) null else value.optLong("boot"), if (value.isNull("uptime_ms")) null else value.optLong("uptime_ms"), if (value.isNull("unix_ms")) null else value.optLong("unix_ms"))
        else -> Point(null, null, null)
    }
    private fun compare(a: Any?, b: Any?): Int {
        val x = point(a); val y = point(b)
        if (x.boot != null && y.boot != null) {
            if (x.boot != y.boot) return x.boot.compareTo(y.boot)
            if (x.uptime != null && y.uptime != null) return x.uptime.compareTo(y.uptime)
        }
        if (x.unix != null && y.unix != null) return x.unix.compareTo(y.unix)
        return 0
    }
    fun faults(previous: JSONArray, incoming: JSONArray): JSONArray {
        val byCode = linkedMapOf<String, JSONObject>()
        for (list in listOf(previous, incoming)) for (i in 0 until list.length()) {
            val current = list.optJSONObject(i) ?: continue
            val key = "${current.optInt("mid")}:${current.optString("identifier_type")}:${current.optInt("identifier")}:${current.optInt("fmi")}"
            val old = byCode[key]
            val latest = if (old == null || compare(current.opt("last"), old.opt("last")) >= 0) current else old
            val first = if (old == null || compare(current.opt("first"), old.opt("first")) < 0) current.opt("first") else old.opt("first")
            byCode[key] = JSONObject(latest.toString()).apply { put("first", first ?: JSONObject.NULL) }
        }
        return JSONArray(byCode.values.toList())
    }
    fun fuelReference(value: JSONObject?, tripUid: String): Double? = value
        ?.takeIf { it.optString("trip_client_uid") == tripUid && it.optBoolean("ecu_reference_fresh") &&
            it.optString("interval_quality") == "DELTA_ECU_EM_VALIDACAO" && !it.isNull("ecu_trip_fuel_l") }
        ?.optDouble("ecu_trip_fuel_l")?.takeIf { it.isFinite() && it >= 0 }
}
