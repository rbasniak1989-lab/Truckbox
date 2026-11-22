package br.com.truckbox.multimedia.next.gps

import br.com.truckbox.multimedia.next.BuildConfig
import org.json.JSONObject
import java.io.IOException
import java.net.URL
import java.time.Instant
import javax.net.ssl.HttpsURLConnection

internal const val GPS_INTERVAL_MS = 10_000L
internal const val GPS_MAX_AGE_MS = 30_000L
internal const val GPS_QUEUE_LIMIT = 4_320
private const val GPS_SOURCE = "ANDROID_MULTIMEDIA"
private const val GPS_PATH = "/functions/v1/truckbox-gps"

internal class GpsCredentials(val deviceUid: String, val token: String) {
    fun isValid(): Boolean = UID.matches(deviceUid) && token.isNotBlank() && token.length <= 256

    private companion object {
        val UID = Regex("[A-Za-z0-9_-]{1,64}")
    }
}

internal data class GpsPoint(val clientUid: String, val capturedAt: Long, val json: String)

internal fun gpsPoint(
    installId: String,
    capturedAt: Long,
    now: Long,
    elapsedAge: Long,
    latitude: Double,
    longitude: Double,
    speedMps: Double?,
    heading: Double?,
    altitude: Double?,
    accuracy: Double?,
): GpsPoint? {
    if (capturedAt < 1704067200000L || now - capturedAt !in -5_000L..GPS_MAX_AGE_MS ||
        elapsedAge !in 0L..GPS_MAX_AGE_MS || latitude !in -90.0..90.0 || longitude !in -180.0..180.0 ||
        (latitude == 0.0 && longitude == 0.0)
    ) return null

    val clientUid = "multimedia-$installId-$capturedAt"
    val json = JSONObject().apply {
        put("client_uid", clientUid)
        put("ts", Instant.ofEpochMilli(capturedAt).toString())
        put("source", GPS_SOURCE)
        put("provider", "gps")
        put("lat", latitude)
        put("lon", longitude)
        speedMps?.let { it * 3.6 }?.takeIf { it.isFinite() && it >= 0 }?.let { put("speed_kmh", it) }
        heading?.takeIf { it >= 0 && it < 360 }?.let { put("heading_deg", it) }
        altitude?.takeIf { it.isFinite() }?.let { put("altitude_m", it) }
        accuracy?.takeIf { it.isFinite() && it >= 0 }?.let { put("accuracy_m", it) }
    }
    return GpsPoint(clientUid, capturedAt, json.toString())
}

internal fun gpsUploadBody(credentials: GpsCredentials, point: GpsPoint): JSONObject =
    JSONObject(point.json).put("device_uid", credentials.deviceUid).put("token", credentials.token)

internal fun gpsAckMatches(response: String, point: GpsPoint): Boolean = runCatching {
    val body = JSONObject(response)
    body.opt("ok") == true && body.optString("truck_id").isNotBlank() &&
        Instant.parse(body.getString("accepted_ts")).toEpochMilli() == point.capturedAt
}.getOrDefault(false)

internal data class GpsUploadResult(val httpStatus: Int, val confirmed: Boolean)

internal fun uploadGps(credentials: GpsCredentials, point: GpsPoint): GpsUploadResult {
    var connection: HttpsURLConnection? = null
    var status = 0
    return try {
        val bytes = gpsUploadBody(credentials, point).toString().toByteArray(Charsets.UTF_8)
        val request = (URL(BuildConfig.CLOUD_URL + GPS_PATH).openConnection() as HttpsURLConnection).also { connection = it }
        request.requestMethod = "POST"
        request.connectTimeout = 4_500
        request.readTimeout = 6_500
        request.instanceFollowRedirects = false
        request.useCaches = false
        request.doOutput = true
        request.setRequestProperty("Content-Type", "application/json")
        request.setFixedLengthStreamingMode(bytes.size)
        request.outputStream.use { it.write(bytes) }
        status = request.responseCode
        if (status !in 200..299) return GpsUploadResult(status, false)

        val reply = ByteArray(4_097)
        val count = request.inputStream.use { stream ->
            var used = 0
            while (used < reply.size) {
                val read = stream.read(reply, used, reply.size - used)
                if (read < 0) break
                used += read
            }
            used
        }
        GpsUploadResult(status, count < reply.size && gpsAckMatches(reply.decodeToString(endIndex = count), point))
    } catch (_: IOException) {
        GpsUploadResult(status, false)
    } finally {
        connection?.disconnect()
    }
}
