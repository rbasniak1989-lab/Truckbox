package br.com.truckbox.app.cloud

import br.com.truckbox.app.operations.TripRecord
import br.com.truckbox.app.preferences.TruckBoxPreferences
import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.util.UUID
import kotlin.math.roundToInt

internal data class TruckRouteProfile(val heightCm: Int, val widthCm: Int, val grossKg: Int, val weightKg: Int) {
    val valid: Boolean
        get() = heightCm in 100..550 && widthCm in 100..450 && grossKg in 3_000..120_000 &&
            weightKg in 2_000..grossKg

    fun toJson() = JSONObject()
        .put("heightCm", heightCm).put("widthCm", widthCm).put("lengthCm", 2_500)
        .put("grossKg", grossKg).put("weightKg", weightKg).put("axles", 9)

    companion object {
        fun fromInput(height: String, width: String, gross: String, current: String): TruckRouteProfile? {
            fun convert(input: String, scale: Int) = input.trim().replace(',', '.').toDoubleOrNull()
                ?.takeIf { it.isFinite() && it > 0 && it <= 150 }?.times(scale)?.roundToInt()
            val heightCm = convert(height, 100) ?: return null
            val widthCm = convert(width, 100) ?: return null
            val grossKg = convert(gross, 1_000) ?: return null
            val weightKg = convert(current, 1_000) ?: return null
            return TruckRouteProfile(heightCm, widthCm, grossKg, weightKg).takeIf { it.valid }
        }
    }
}

internal data class RouteOption(
    val id: String,
    val provider: String,
    val origin: String?,
    val destination: String?,
    val distanceM: Int,
    val durationSec: Int,
    val warnings: List<String>,
) {
    val remaining: Boolean get() = provider == "HERE" && origin == "Posição do caminhão"
}

internal data class RouteToll(val name: String, val atM: Int?, val freeFlow: Boolean, val lowCents: Int?, val highCents: Int?)

internal data class TripRoute(
    val option: RouteOption,
    val polyline: String,
    val createdAt: Instant,
    val validUntil: Instant?,
    val tollCoverage: String,
    val tolls: List<RouteToll>,
)

internal data class TripRoutes(val linked: TripRoute?, val options: List<RouteOption>)

internal class TripRoutesClient(
    private val prefs: TruckBoxPreferences,
    private val transport: DriverCloudTransport = DriverCloudTransport(),
) {
    fun list(trip: TripRecord): TripRoutes {
        val result = post("plans", trip)
        val options = result.getJSONArray("plans").objects(::option)
        require(result.has("route"))
        val linked = if (result.isNull("route")) null else route(result.getJSONObject("route"), trip)
        return TripRoutes(linked, options)
    }

    fun calculate(trip: TripRecord, profile: TruckRouteProfile, fromGps: Boolean): TripRoute {
        require(profile.valid)
        val payload = JSONObject()
            .put("clientId", UUID.randomUUID().toString()).put("useGps", fromGps).put("profile", profile.toJson())
        return route(post("plan_truck", trip, payload).getJSONObject("route"), trip)
    }

    fun attach(trip: TripRecord, selected: RouteOption) {
        val payload = JSONObject().put("id", selected.id).put("provider", selected.provider)
        if (!post("attach", trip, payload).optBoolean("ok", false)) throw DriverCloudException(502)
    }

    private fun post(action: String, trip: TripRecord, payload: JSONObject = JSONObject()): JSONObject {
        require(trip.cloudId != null && trip.status == "ACTIVE" && trip.clientUid.matches(Regex("[-A-Za-z0-9_]{1,128}")))
        val credentials = prefs.cloudCredentials()
        if (!credentials.configured) throw DriverCloudException(401)
        val request = JSONObject().put("device_uid", credentials.deviceUid).put("token", credentials.token)
            .put("action", action).put("payload", payload.put("tripClientUid", trip.clientUid))
        val result = transport.post(DriverEndpoint.TRIP_ROUTES, request, 35_000)
        if (credentials != prefs.cloudCredentials()) throw DriverCloudException(401)
        return result
    }

    private fun option(value: JSONObject): RouteOption {
        val id = value.getString("id")
        val provider = value.getString("provider")
        val distance = value.getInt("distanceM")
        val duration = value.getInt("durationSec")
        require(UUID.fromString(id).toString().equals(id, ignoreCase = true) && provider in setOf("GOOGLE", "HERE"))
        require(distance in 1..25_000_000 && duration in 1..1_209_600)
        val warnings = value.optJSONArray("warnings") ?: JSONArray()
        return RouteOption(id, provider, value.text("origin"), value.text("destination"), distance, duration,
            (0 until warnings.length()).mapNotNull { warnings.optString(it).takeIf(String::isNotBlank) })
    }

    private fun route(value: JSONObject, trip: TripRecord): TripRoute {
        require(value.getString("tripId") == trip.cloudId)
        val polyline = value.getString("polyline")
        require(polyline.length in 4..42_000)
        return TripRoute(option(value), polyline, cloudInstant(value.getString("createdAt")),
            value.text("validUntil")?.let(::cloudInstant), value.getString("tollCoverage"),
            value.getJSONArray("tolls").objects { item ->
                RouteToll(item.getString("name"), item.nonNegativeInt("atM"), item.optString("kind") == "FREE_FLOW",
                    item.nonNegativeInt("lowCents"), item.nonNegativeInt("highCents"))
            })
    }
}

private fun JSONObject.text(key: String) = if (isNull(key)) null else optString(key).takeIf(String::isNotBlank)
private fun JSONObject.nonNegativeInt(key: String) = optInt(key, -1).takeIf { it >= 0 }
private fun <T> JSONArray.objects(read: (JSONObject) -> T): List<T> = List(length()) { read(getJSONObject(it)) }
