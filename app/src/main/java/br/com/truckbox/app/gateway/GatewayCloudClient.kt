package br.com.truckbox.app.gateway

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import br.com.truckbox.app.preferences.TruckBoxPreferences
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

data class CloudBatchResult(val ackSeq: Long, val accepted: Int, val health: Int)

class GatewayCloudClient(context: Context, private val prefs: TruckBoxPreferences) {
    companion object {
        private const val INGEST = "https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-ingest"
        private const val GPS = "https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-gps"
    }

    private val cm = context.applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    @Volatile
    var lastError: String? = null
        private set

    @Synchronized
    fun upload(samples: JSONArray, expectedDeviceUid: String): CloudBatchResult? {
        if (samples.length() == 0) {
            lastError = "Lote vazio"
            return null
        }
        if (prefs.cloudDeviceToken.isBlank()) {
            lastError = "Token Cloud do Core não configurado"
            return null
        }
        if (prefs.cloudDeviceUid.isBlank()) {
            lastError = "Device UID da Cloud não configurado"
            return null
        }
        if (expectedDeviceUid.isNotBlank() && expectedDeviceUid != prefs.cloudDeviceUid) {
            lastError = "UID diferente: Core=$expectedDeviceUid / App=${prefs.cloudDeviceUid}"
            return null
        }

        val body = JSONObject().apply {
            put("device_uid", prefs.cloudDeviceUid)
            put("token", prefs.cloudDeviceToken)
            put("samples", samples)
        }.toString()

        val j = post(INGEST, body) ?: return null
        if (!j.optBoolean("ok", false)) {
            lastError = "Cloud respondeu sem ok=true"
            return null
        }

        lastError = null
        return CloudBatchResult(
            ackSeq = j.optLong("ack_seq", 0L),
            accepted = j.optInt("accepted", 0),
            health = j.optInt("health", 0),
        )
    }

    @Synchronized
    fun uploadGps(location: android.location.Location, installId: String): Boolean {
        if (prefs.gpsDeviceUid.isBlank() || prefs.gpsDeviceToken.isBlank()) return false

        val body = JSONObject().apply {
            put("device_uid", prefs.gpsDeviceUid)
            put("token", prefs.gpsDeviceToken)
            put("source", "ANDROID_PHONE")
            put("client_uid", "$installId:${location.time}")
            put("ts", java.time.Instant.ofEpochMilli(location.time).toString())
            put("lat", location.latitude)
            put("lon", location.longitude)
            put("speed_kmh", if (location.hasSpeed()) location.speed * 3.6 else JSONObject.NULL)
            put("heading_deg", if (location.hasBearing()) location.bearing else JSONObject.NULL)
            put("altitude_m", if (location.hasAltitude()) location.altitude else JSONObject.NULL)
            put("accuracy_m", if (location.hasAccuracy()) location.accuracy else JSONObject.NULL)
            put("provider", location.provider ?: "android")
        }.toString()

        return post(GPS, body)?.optBoolean("ok", false) == true
    }

    /**
     * Cloud nunca deve ficar presa à rede local do ESP32.
     *
     * Ordem:
     * 1) rede ativa validada;
     * 2) qualquer outra rede validada com INTERNET;
     * 3) rede ativa que ao menos declare INTERNET;
     * 4) rota padrão do Android como último fallback.
     */
    private fun cloudNetworks(): List<Network?> {
        val result = linkedSetOf<Network?>()
        return runCatching {
            val active = cm.activeNetwork

            fun caps(n: Network?): NetworkCapabilities? = n?.let(cm::getNetworkCapabilities)
            fun internet(c: NetworkCapabilities?) =
                c?.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) == true
            fun validated(c: NetworkCapabilities?) =
                c?.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) == true

            val activeCaps = caps(active)
            if (active != null && internet(activeCaps) && validated(activeCaps)) result += active

            cm.allNetworks.forEach { n ->
                val c = caps(n)
                if (internet(c) && validated(c)) result += n
            }

            if (active != null && internet(activeCaps)) result += active
            result += null
            result.toList()
        }.getOrElse {
            listOf(null)
        }
    }

    private fun post(endpoint: String, body: String): JSONObject? {
        var failure: String? = null

        for (network in cloudNetworks()) {
            try {
                val url = URL(endpoint)
                val c = ((network?.openConnection(url) ?: url.openConnection()) as HttpURLConnection).apply {
                    requestMethod = "POST"
                    connectTimeout = 5_000
                    readTimeout = 8_000
                    doOutput = true
                    useCaches = false
                    instanceFollowRedirects = false
                    setRequestProperty("Content-Type", "application/json")
                    setRequestProperty("Connection", "close")
                }

                c.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }

                val code = c.responseCode
                val stream = if (code in 200..299) c.inputStream else c.errorStream
                val text = stream?.bufferedReader()?.use { it.readText() }.orEmpty()
                c.disconnect()

                if (code in 200..299 && text.isNotBlank()) {
                    val parsed = JSONObject(text)
                    lastError = null
                    return parsed
                }

                failure = buildString {
                    append("HTTP ").append(code)
                    if (text.isNotBlank()) append(": ").append(text.take(160))
                }

                if (code in 400..499) break
            } catch (t: Throwable) {
                val detail = t.message?.take(160).orEmpty()
                failure = if (detail.isBlank()) t.javaClass.simpleName else "${t.javaClass.simpleName}: $detail"
            }
        }

        lastError = failure ?: "Falha de rede sem detalhe"
        return null
    }
}
