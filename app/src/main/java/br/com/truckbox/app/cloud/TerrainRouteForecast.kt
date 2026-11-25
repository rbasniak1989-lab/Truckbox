package br.com.truckbox.app.cloud

import org.json.JSONObject

internal data class RouteElevation(val distanceM: Double, val altitudeM: Double)

internal data class TerrainRouteForecast(
    val modelVersion: String,
    val confidence: String,
    val historySamples: Int,
    val liters: Double,
    val averageKml: Double?,
    val fuelCostCents: Long?,
    val operatingCostCents: Long?,
    val operatingCostComplete: Boolean,
    val tollPayer: String?,
    val balanceAtArrivalLiters: Double?,
    val reserveLiters: Double?,
    val refuelRequired: Boolean?,
)

internal fun JSONObject.terrainForecast(routeId: String, tripId: String, revision: Int?): TerrainRouteForecast? {
    val prediction = optJSONObject("forecast") ?: return null
    if (prediction.optString("method") != "TRUCK_TERRAIN_HISTORY_V1" ||
        prediction.optString("routeId") != routeId || prediction.optString("tripId") != tripId ||
        revision == null || prediction.optInt("routeRevision", -1) != revision) return null
    val confidence = prediction.optString("confidence").takeIf { it in setOf("HIGH", "MEDIUM", "LOW") } ?: return null
    val model = prediction.optString("modelVersion").takeIf { it.length in 1..64 } ?: return null
    val samples = prediction.optInt("historySamples", -1).takeIf { it >= 0 } ?: return null
    if (samples == 0 && confidence != "LOW") return null
    val liters = prediction.number("fuelLiters")?.takeIf { it >= 0 } ?: return null
    val finance = prediction.optJSONObject("cost")
    val operating = finance?.optLong("operatingCents", -1)?.takeIf { it >= 0 }
    val payer = finance?.optString("tollPayer")?.takeIf { it in setOf("CARRIER", "DRIVER", "UNKNOWN") }
    return TerrainRouteForecast(model, confidence, samples, liters, prediction.number("averageKml")?.takeIf { it > 0 },
        finance?.optLong("fuelCents", -1)?.takeIf { it >= 0 }, operating,
        finance?.optBoolean("complete") == true && operating != null && payer in setOf("CARRIER", "DRIVER"), payer,
        prediction.number("balanceAtArrivalLiters"), prediction.number("reserveLiters")?.takeIf { it >= 0 },
        prediction.opt("refuelRequired") as? Boolean)
}

internal fun JSONObject.routeElevation(distanceM: Int): List<RouteElevation> {
    val array = optJSONArray("elevation") ?: return emptyList()
    if (array.length() !in 2..1_200) return emptyList()
    val points = (0 until array.length()).map { i ->
        val point = array.optJSONObject(i) ?: return emptyList()
        val at = point.number("distanceM")?.takeIf { it in 0.0..distanceM.toDouble() } ?: return emptyList()
        val altitude = point.number("altitudeM")?.takeIf { it in -500.0..9_000.0 } ?: return emptyList()
        RouteElevation(at, altitude)
    }
    return points.takeIf { it.zipWithNext().all { (a, b) -> a.distanceM < b.distanceM } }.orEmpty()
}

internal fun JSONObject.number(key: String): Double? = optDouble(key).takeIf(Double::isFinite)
