package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.LocalFuelWindow
import br.com.truckbox.multimedia.next.domain.LocalTripFuel
import br.com.truckbox.multimedia.next.domain.validCoreHost
import com.google.gson.JsonParser
import java.net.URL
import java.time.Instant

internal fun parseCoreFuel(body: String, uid: String, received: Instant): LocalFuelWindow? = runCatching {
    val value = JsonParser.parseString(body).asJsonObject
    if (value.get("contract").asString != "TBX_FUEL_10S_V2" || value.get("device_uid").asString != uid) return null
    val rows = value.getAsJsonArray("w183")
    if (rows.size() != 1) return null
    val row = rows[0].asJsonArray
    if (row.size() != 8 && row.size() != 14) return null
    fun integer(index: Int) = row[index].asBigDecimal.longValueExact()
    val slot = integer(0).takeIf { it in 1..4294967295L } ?: return null
    val uptime = value.get("uptime_ms").asBigDecimal.longValueExact()
    val age = uptime - slot * 10000
    if (age !in 0..30000) return null
    val covered = integer(3).takeIf { it in 0..10000 } ?: return null
    val microlitres = integer(2).takeIf { it in 0..(covered * 1000000 + 3599) / 3600 } ?: return null
    val trip = runCatching localTrip@{
        val local = value.getAsJsonObject("local_trip") ?: return@localTrip null
        val litres = local.get("litres").asDouble.takeIf {it in 0.0..1e9} ?: return@localTrip null
        val km = local.get("distance_km")?.asDouble?.takeIf {it in 0.0..1e9}
        val totalCoverage = local.get("covered_ms")?.asBigDecimal?.longValueExact()?.takeIf { it in 0..uptime }
        LocalTripFuel(km, litres, local.get("gaps").asBoolean, totalCoverage)
    }.getOrNull()
    val boot = value.get("boot_id")?.asBigDecimal?.longValueExact()?.takeIf { it in 0..4294967295L }
    LocalFuelWindow(uid, received.minusMillis(age), covered, microlitres, trip, boot, uptime, slot)
}.getOrNull()

class CoreFuelClient(private val connections: CoreConnectionFactory, private val now: () -> Instant = Instant::now) {
    fun fetch(host: String, uid: String): LocalFuelWindow? {
        if (!validCoreHost(host) || uid.isBlank()) return null
        for (candidate in linkedSetOf(host, "192.168.4.1", "truckbox.local")) {
            val result = runCatching {
                val connection = connections.open(URL("http://$candidate/api/fuel"))
                try {
                    connection.connectTimeout = 1100
                    connection.readTimeout = 1400
                    connection.instanceFollowRedirects = false
                    connection.useCaches = false
                    if (connection.responseCode == 200) {
                        val buffer = ByteArray(1537)
                        var used = 0
                        connection.inputStream.use { input ->
                            while (used < buffer.size) {
                                val bytes = input.read(buffer, used, buffer.size - used)
                                if (bytes <= 0) break
                                used += bytes
                            }
                        }
                        if (used <= 1536) parseCoreFuel(String(buffer, 0, used, Charsets.UTF_8), uid, now()) else null
                    } else null
                } finally { connection.disconnect() }
            }.getOrNull()
            if (result != null) return result
        }
        return null
    }
}
