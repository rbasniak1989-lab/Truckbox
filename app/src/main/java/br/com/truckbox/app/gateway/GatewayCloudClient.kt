package br.com.truckbox.app.gateway

import br.com.truckbox.app.preferences.TruckBoxPreferences
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

data class CloudBatchResult(val ackSeq: Long, val accepted: Int, val health: Int)

class GatewayCloudClient(private val prefs: TruckBoxPreferences) {
    companion object {
        private const val INGEST = "https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-ingest"
        private const val GPS = "https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-gps"
    }

    fun upload(samples: JSONArray, expectedDeviceUid: String): CloudBatchResult? {
        if (samples.length() == 0 || prefs.cloudDeviceToken.isBlank() || prefs.cloudDeviceUid.isBlank()) return null
        if (expectedDeviceUid.isNotBlank() && expectedDeviceUid != prefs.cloudDeviceUid) return null
        val body = JSONObject().apply {
            put("device_uid", prefs.cloudDeviceUid)
            put("token", prefs.cloudDeviceToken)
            put("samples", samples)
        }.toString()
        val j = post(INGEST, body) ?: return null
        if (!j.optBoolean("ok", false)) return null
        return CloudBatchResult(
            ackSeq = j.optLong("ack_seq", 0L),
            accepted = j.optInt("accepted", 0),
            health = j.optInt("health", 0),
        )
    }

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

    private fun post(endpoint: String, body: String): JSONObject? = try {
        val c = URL(endpoint).openConnection() as HttpURLConnection
        c.requestMethod = "POST"
        c.connectTimeout = 4500
        c.readTimeout = 6500
        c.doOutput = true
        c.useCaches = false
        c.setRequestProperty("Content-Type", "application/json")
        c.setRequestProperty("Connection", "close")
        c.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
        val code = c.responseCode
        val stream = if (code in 200..299) c.inputStream else c.errorStream
        val text = stream?.bufferedReader()?.use { it.readText() }.orEmpty()
        c.disconnect()
        if (code in 200..299 && text.isNotBlank()) JSONObject(text) else null
    } catch (_: Throwable) { null }
}
