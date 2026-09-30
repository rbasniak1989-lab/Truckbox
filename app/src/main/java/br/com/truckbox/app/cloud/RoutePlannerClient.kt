package br.com.truckbox.app.cloud

import br.com.truckbox.app.operations.RoutePlanSnapshot
import br.com.truckbox.app.preferences.TruckBoxPreferences
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID

data class RoutePlanResponse(
    val plan: RoutePlanSnapshot? = null,
    val error: String? = null,
)

class RoutePlannerClient(private val prefs: TruckBoxPreferences) {
    companion object {
        private const val ENDPOINT = "https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-app"
    }

    fun calculate(
        sourceUrl: String,
        origin: String,
        destination: String,
        weightT: Double,
        ratePerT: Double?,
    ): RoutePlanResponse {
        if (!configured()) return RoutePlanResponse(error = "Configure o TruckBox Cloud primeiro.")
        val clientUid = UUID.randomUUID().toString()
        val response = post("calculate_route", JSONObject().apply {
            put("client_uid", clientUid)
            put("source_url", sourceUrl.trim())
            put("origin", origin.trim())
            put("destination", destination.trim())
            put("weight_t", weightT)
            putNullable("rate_per_t", ratePerT)
        }) ?: return RoutePlanResponse(error = "Sem resposta da nuvem.")

        if (!response.optBoolean("ok", false)) {
            val code = response.optString("error", "route_calculation_failed")
            val detail = response.optString("detail", "")
            val message = when (code) {
                "google_maps_api_key_missing" -> "Falta configurar a chave do Google Maps no TruckBox Cloud."
                "invalid_route_inputs" -> "Confira origem, destino e peso."
                "route_polyline_missing" -> "O Google não retornou o traçado da rota."
                else -> if (detail.isNotBlank()) "$code: $detail" else code
            }
            return RoutePlanResponse(error = message)
        }

        val o = response.optJSONObject("simulation")
            ?: return RoutePlanResponse(error = "Resposta da rota incompleta.")
        val g = o.optJSONObject("geography")
        val b = o.optJSONObject("prediction_basis")
        return RoutePlanResponse(
            plan = RoutePlanSnapshot(
                simulationId = o.optString("id"),
                clientUid = o.optString("client_uid", clientUid),
                sourceUrl = o.optString("source_url", sourceUrl),
                origin = o.optString("origin", origin),
                destination = o.optString("destination", destination),
                cargoWeightT = o.optDouble("cargo_weight_t", weightT),
                tareWeightT = o.optDouble("tare_weight_t", 0.0),
                grossWeightT = o.optDouble("gross_weight_t", weightT),
                ratePerT = o.optNullableDouble("rate_per_t"),
                freightTotal = o.optNullableDouble("freight_total"),
                routeDistanceKm = o.optNullableDouble("route_distance_km"),
                ascentM = g?.optNullableDouble("ascent_m"),
                descentM = g?.optNullableDouble("descent_m"),
                expectedKml = o.optNullableDouble("expected_kml"),
                predictedFuelLiters = o.optNullableDouble("predicted_fuel_liters"),
                predictedFuelLowL = o.optNullableDouble("predicted_fuel_low_l"),
                predictedFuelHighL = o.optNullableDouble("predicted_fuel_high_l"),
                confidence = o.optString("confidence", "low"),
                routeMemoryCoveragePct = b?.optNullableDouble("route_memory_coverage_pct"),
                geographyModelCoveragePct = b?.optNullableDouble("geography_model_coverage_pct"),
                predictionBasisJson = b?.toString(),
            )
        )
    }

    fun attachToTrip(simulationId: String, tripClientUid: String): Boolean {
        if (!configured()) return false
        return post("attach_route_plan", JSONObject().apply {
            put("simulation_id", simulationId)
            put("trip_client_uid", tripClientUid)
        })?.optBoolean("ok", false) == true
    }

    private fun configured(): Boolean =
        prefs.cloudDeviceUid.isNotBlank() && prefs.cloudDeviceToken.isNotBlank()

    private fun post(action: String, payload: JSONObject): JSONObject? {
        val body = JSONObject().apply {
            put("device_uid", prefs.cloudDeviceUid)
            put("token", prefs.cloudDeviceToken)
            put("action", action)
            put("payload", payload)
        }.toString()
        return try {
            val c = URL(ENDPOINT).openConnection() as HttpURLConnection
            c.requestMethod = "POST"
            c.connectTimeout = 8000
            c.readTimeout = 45_000
            c.doOutput = true
            c.setRequestProperty("Content-Type", "application/json")
            c.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
            val code = c.responseCode
            val stream = if (code in 200..299) c.inputStream else c.errorStream
            val text = stream?.bufferedReader()?.use { it.readText() }.orEmpty()
            c.disconnect()
            if (text.isBlank()) null else runCatching { JSONObject(text) }.getOrNull()
        } catch (_: Throwable) {
            null
        }
    }
}

private fun JSONObject.putNullable(key: String, value: Any?) {
    if (value != null) put(key, value) else put(key, JSONObject.NULL)
}

private fun JSONObject.optNullableDouble(key: String): Double? =
    if (isNull(key) || !has(key)) null else optDouble(key).takeIf { it.isFinite() }
