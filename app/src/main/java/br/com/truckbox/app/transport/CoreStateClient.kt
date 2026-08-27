package br.com.truckbox.app.transport

import android.content.Context
import br.com.truckbox.app.preferences.TruckBoxPreferences
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL


data class CoreSessionTotals(
    val fuelLiters: Double?,
    val distanceKm: Double?,
    val odometerKm: Double?,
)

/** Consulta os acumuladores persistentes do Core para recuperar períodos sem tela. */
class CoreStateClient(context: Context, private val prefs: TruckBoxPreferences) {
    private val binder = WifiNetworkBinder(context)

    fun resetSession(): Boolean {
        val hosts = linkedSetOf(prefs.coreHost, "truckbox.local", "192.168.4.1").filter { it.isNotBlank() }
        for (hostRaw in hosts) {
            val base = if (hostRaw.startsWith("http://") || hostRaw.startsWith("https://")) hostRaw.trimEnd('/') else "http://$hostRaw"
            try {
                val c = binder.openWifiHttp(URL("$base/api/reset"))
                c.requestMethod = "POST"; c.connectTimeout = 1200; c.readTimeout = 1600; c.doOutput = true
                c.outputStream.use { }
                val ok = c.responseCode in 200..299
                c.disconnect()
                if (ok) return true
            } catch (_: Throwable) { }
        }
        return false
    }

    fun fetch(): CoreSessionTotals? {
        val hosts = linkedSetOf(prefs.coreHost, "truckbox.local", "192.168.4.1").filter { it.isNotBlank() }
        for (hostRaw in hosts) {
            val base = if (hostRaw.startsWith("http://") || hostRaw.startsWith("https://")) hostRaw.trimEnd('/') else "http://$hostRaw"
            try {
                val c = binder.openWifiHttp(URL("$base/api/state"))
                c.requestMethod = "GET"; c.connectTimeout = 1200; c.readTimeout = 1600; c.useCaches = false
                if (c.responseCode in 200..299) {
                    val text = c.inputStream.bufferedReader().use { it.readText() }
                    c.disconnect()
                    val j = JSONObject(text)
                    return CoreSessionTotals(
                        fuelLiters = j.optFiniteDouble("fuelLitersTest"),
                        distanceKm = j.optFiniteDouble("distanceTestKm"),
                        odometerKm = j.optFiniteDouble("distanceTotalKm"),
                    )
                }
                c.disconnect()
            } catch (_: Throwable) { }
        }
        return null
    }
}

private fun JSONObject.optFiniteDouble(key: String): Double? {
    if (!has(key) || isNull(key)) return null
    val v = optDouble(key, Double.NaN)
    return v.takeIf { it.isFinite() }
}
