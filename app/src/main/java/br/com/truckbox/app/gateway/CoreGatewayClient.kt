package br.com.truckbox.app.gateway

import android.content.Context
import android.location.Location
import br.com.truckbox.app.preferences.TruckBoxPreferences
import br.com.truckbox.app.transport.WifiNetworkBinder
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

data class GatewayBatch(
    val deviceUid: String,
    val samples: JSONArray,
    val count: Int,
    val maxSeq: Long,
    val pendingBytes: Long,
    val lastAckSeq: Long,
)

data class CoreLiveSnapshot(
    val odometerKm: Double?,
    val coreFuelLiters: Double?,
    val speedKmh: Double?,
    val rpm: Double?,
    val combinationWeightKg: Double?,
)

class CoreGatewayClient(context: Context, private val prefs: TruckBoxPreferences) {
    private val binder = WifiNetworkBinder(context)
    @Volatile private var lastWorkingBase: String? = null

    private fun bases(): List<String> {
        val items = linkedSetOf<String>()
        lastWorkingBase?.let { items += it }
        val hosts = linkedSetOf(prefs.coreHost, "truckbox.local", "192.168.4.1")
        hosts.filter { it.isNotBlank() }.forEach { raw ->
            items += if (raw.startsWith("http://") || raw.startsWith("https://")) raw.trimEnd('/') else "http://${raw.trimEnd('/')}"
        }
        return items.toList()
    }

    private fun open(url: URL): HttpURLConnection = binder.openWifiHttp(url).apply {
        connectTimeout = 1500
        readTimeout = 2500
        useCaches = false
        setRequestProperty("Connection", "close")
    }

    fun fetchBatch(limit: Int = 8): GatewayBatch? {
        for (base in bases()) {
            try {
                val c = open(URL("$base/api/gateway/batch?limit=${limit.coerceIn(1, 8)}"))
                c.requestMethod = "GET"
                val code = c.responseCode
                val body = (if (code in 200..299) c.inputStream else c.errorStream)?.bufferedReader()?.use { it.readText() }.orEmpty()
                c.disconnect()
                if (code !in 200..299 || body.isBlank()) continue
                val j = JSONObject(body)
                if (!j.optBoolean("ok", false)) continue
                lastWorkingBase = base
                return GatewayBatch(
                    deviceUid = j.optString("device_uid"),
                    samples = j.optJSONArray("samples") ?: JSONArray(),
                    count = j.optInt("count", 0),
                    maxSeq = j.optLong("max_seq", 0L),
                    pendingBytes = j.optLong("pending_bytes", 0L),
                    lastAckSeq = j.optLong("last_ack_seq", 0L),
                )
            } catch (_: Throwable) { }
        }
        return null
    }

    fun ack(seq: Long): Boolean {
        if (seq <= 0L) return false
        val form = "ack_seq=" + URLEncoder.encode(seq.toString(), Charsets.UTF_8.name())
        for (base in bases()) {
            try {
                val c = open(URL("$base/api/gateway/ack"))
                c.requestMethod = "POST"
                c.doOutput = true
                c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                c.outputStream.use { it.write(form.toByteArray(Charsets.UTF_8)) }
                val ok = c.responseCode in 200..299
                c.disconnect()
                if (ok) { lastWorkingBase = base; return true }
            } catch (_: Throwable) { }
        }
        return false
    }

    fun fetchLive(): CoreLiveSnapshot? {
        for (base in bases()) {
            try {
                val c = open(URL("$base/api/state?nocache=${System.currentTimeMillis()}"))
                c.requestMethod = "GET"
                if (c.responseCode !in 200..299) { c.disconnect(); continue }
                val text = c.inputStream.bufferedReader().use { it.readText() }
                c.disconnect()
                val j = JSONObject(text)
                lastWorkingBase = base
                return CoreLiveSnapshot(
                    odometerKm = j.finite("distanceTotalKm"),
                    coreFuelLiters = j.finite("fuelLitersTest"),
                    speedKmh = j.finite("speedKph"),
                    rpm = j.finite("rpm"),
                    combinationWeightKg = j.finite("combinationWeightKg"),
                )
            } catch (_: Throwable) { }
        }
        return null
    }

    fun pushGps(location: Location): Boolean {
        val args = linkedMapOf(
            "lat" to location.latitude.toString(),
            "lon" to location.longitude.toString(),
            "speed_kmh" to (if (location.hasSpeed()) (location.speed * 3.6).toString() else ""),
            "bearing_deg" to (if (location.hasBearing()) location.bearing.toString() else ""),
            "altitude_m" to (if (location.hasAltitude()) location.altitude.toString() else ""),
            "accuracy_m" to (if (location.hasAccuracy()) location.accuracy.toString() else ""),
            "time_ms" to location.time.toString(),
        )
        val form = args.entries.joinToString("&") { (k, v) ->
            URLEncoder.encode(k, Charsets.UTF_8.name()) + "=" + URLEncoder.encode(v, Charsets.UTF_8.name())
        }
        for (base in bases()) {
            try {
                val c = open(URL("$base/api/gps"))
                c.requestMethod = "POST"
                c.doOutput = true
                c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                c.outputStream.use { it.write(form.toByteArray(Charsets.UTF_8)) }
                val ok = c.responseCode in 200..299
                c.disconnect()
                if (ok) { lastWorkingBase = base; return true }
            } catch (_: Throwable) { }
        }
        return false
    }
}

private fun JSONObject.finite(key: String): Double? {
    if (!has(key) || isNull(key)) return null
    return optDouble(key, Double.NaN).takeIf { it.isFinite() }
}
