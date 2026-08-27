package br.com.truckbox.app.cloud

import br.com.truckbox.app.operations.*
import br.com.truckbox.app.preferences.TruckBoxPreferences
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/**
 * Sincronização operacional best-effort da v0.4.1.
 * A persistência local acontece primeiro; falha de internet nunca impede o motorista
 * de cadastrar uma viagem/abastecimento/manutenção.
 */
class TruckBoxCloudClient(private val prefs: TruckBoxPreferences) {
    companion object {
        private const val ENDPOINT = "https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-app"
    }

    fun configured(): Boolean = prefs.cloudDeviceUid.isNotBlank() && prefs.cloudDeviceToken.isNotBlank()

    fun syncTrip(t: TripRecord) = post("upsert_trip", JSONObject().apply {
        put("client_uid", t.clientUid); put("status", t.status); put("origin", t.origin); put("destination", t.destination)
        put("started_at", iso(t.startedAtMs)); putNullable("ended_at", t.endedAtMs?.let(::iso))
        putNullable("start_odometer_km", t.startOdometerKm); putNullable("end_odometer_km", t.endOdometerKm)
        put("start_core_fuel_liters", t.startFuelCounterL); putNullable("end_core_fuel_liters", t.endFuelCounterL)
        putNullable("weight_t", t.weightT); putNullable("rate_per_t", t.ratePerT); putNullable("freight_total", t.freightTotal)
        putNullable("distance_km", t.distanceKm); putNullable("fuel_liters", t.fuelLiters); putNullable("average_kml", t.averageKml)
    })

    fun endTrip(t: TripRecord) = post("end_trip", JSONObject().apply {
        put("client_uid", t.clientUid); putNullable("ended_at", t.endedAtMs?.let(::iso)); putNullable("end_odometer_km", t.endOdometerKm)
        putNullable("end_core_fuel_liters", t.endFuelCounterL); putNullable("distance_km", t.distanceKm); putNullable("fuel_liters", t.fuelLiters); putNullable("average_kml", t.averageKml)
    })

    fun syncFueling(f: FuelingRecord) = post("upsert_fueling", JSONObject().apply {
        put("client_uid", f.clientUid); put("ts", iso(f.tsMs)); put("liters", f.liters); put("price_per_l", f.pricePerL); put("total", f.total)
        put("station", f.station); put("location", f.location); put("full_tank", f.fullTank); putNullable("odometer_km", f.odometerKm)
    })

    fun syncExpense(e: ExpenseRecord) = post("upsert_expense", JSONObject().apply {
        put("client_uid", e.clientUid); put("ts", iso(e.tsMs)); put("category", e.category); put("description", e.description); put("amount", e.amount); putNullable("trip_client_uid", e.tripClientUid)
    })

    fun syncMaintenance(m: MaintenanceRecord) = post("upsert_maintenance", JSONObject().apply {
        put("client_uid", m.clientUid); put("service_type", m.serviceType); put("performed_at", iso(m.performedAtMs)); putNullable("odometer_km", m.odometerKm)
        putNullable("next_due_km", m.nextDueKm); putNullable("cost", m.cost); put("notes", m.notes)
    })

    fun syncTank(capacityL: Double, estimatedL: Double?, fuelCounterL: Double) = post("set_tank_state", JSONObject().apply {
        put("capacity_l", capacityL); putNullable("estimated_l", estimatedL); put("fuel_counter_l", fuelCounterL)
    })

    fun snapshot(): JSONObject? = post("snapshot", JSONObject())

    private fun post(action: String, payload: JSONObject): JSONObject? {
        if (!configured()) return null
        val body = JSONObject().apply {
            put("device_uid", prefs.cloudDeviceUid)
            put("token", prefs.cloudDeviceToken)
            put("action", action)
            put("payload", payload)
        }.toString()
        return try {
            val c = URL(ENDPOINT).openConnection() as HttpURLConnection
            c.requestMethod = "POST"; c.connectTimeout = 3500; c.readTimeout = 5000; c.doOutput = true
            c.setRequestProperty("Content-Type", "application/json")
            c.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
            val code = c.responseCode
            val stream = if (code in 200..299) c.inputStream else c.errorStream
            val text = stream?.bufferedReader()?.use { it.readText() }.orEmpty()
            c.disconnect()
            if (code in 200..299 && text.isNotBlank()) JSONObject(text) else null
        } catch (_: Throwable) { null }
    }

    private fun iso(ms: Long): String = java.time.Instant.ofEpochMilli(ms).toString()
}

private fun JSONObject.putNullable(key: String, value: Any?) {
    if (value != null) put(key, value) else put(key, JSONObject.NULL)
}
