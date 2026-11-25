package br.com.truckbox.app

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import br.com.truckbox.app.cloud.DriverCloudException
import br.com.truckbox.app.cloud.TripRoutesClient
import br.com.truckbox.app.cloud.TruckRouteProfile
import br.com.truckbox.app.operations.TripRecord
import br.com.truckbox.app.ui.decodeRoutePolyline
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

private const val ROUTE = "00000000-0000-4000-8000-000000000101"
private const val TRIP = "00000000-0000-4000-8000-000000000102"
private const val GOOGLE_ROUTE = "00000000-0000-4000-8000-000000000103"

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class TripRoutesClientTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private val trip = TripRecord(clientUid = "cloud-trip", origin = "Origem", destination = "Destino",
        startedAtMs = TEST_TIME, cloudId = TRIP, weightT = 41.0, distanceKm = 64.3, fuelLiters = 25.0)

    @Before fun setup() {
        context.getSharedPreferences("truckbox", 0).edit().clear().commit()
    }

    @Test fun listCalculateAttachAndReadBackUseOnlyTheCurrentTripRouteApi() {
        var attached = false
        var calculated = false
        val fake = FakeCloud { url, body ->
            assertEquals("/functions/v1/truckbox-trip-v1", url.path)
            val payload = body.getJSONObject("payload")
            assertEquals(trip.clientUid, payload.getString("tripClientUid"))
            when (body.getString("action")) {
                "plans" -> 200 to JSONObject().put("route", if (attached) route() else JSONObject.NULL)
                    .put("plans", JSONArray().put(option(GOOGLE_ROUTE, "GOOGLE")).apply { if (calculated) put(route()) }).toString()
                "plan_truck" -> {
                    assertTrue(payload.getBoolean("useGps"))
                    assertEquals(3, payload.getInt("alternatives"))
                    val profile = payload.getJSONObject("profile")
                    assertEquals(2500, profile.getInt("lengthCm"))
                    assertEquals(9, profile.getInt("axles"))
                    assertEquals(74_000, profile.getInt("grossKg"))
                    assertEquals(68_370, profile.getInt("weightKg"))
                    assertEquals(425, profile.getInt("heightCm"))
                    assertEquals(260, profile.getInt("widthCm"))
                    calculated = true
                    200 to JSONObject().put("route", route()).toString()
                }
                "attach" -> {
                    assertEquals(setOf("tripClientUid", "id", "provider"), payload.keys().asSequence().toSet())
                    assertEquals(ROUTE, payload.getString("id"))
                    assertEquals("HERE", payload.getString("provider"))
                    attached = true
                    200 to "{\"ok\":true}"
                }
                else -> error("Unexpected mutation")
            }
        }
        val client = TripRoutesClient(prefs(context), fake.transport())
        val initial = client.list(trip)
        assertNull(initial.linked)
        assertEquals(listOf(GOOGLE_ROUTE), initial.options.map { it.id })
        val profile = TruckRouteProfile.fromInput("4,25", "2,60", "74", "68,37")!!
        val result = client.calculate(trip, profile, fromGps = true).single()
        assertFalse(attached)
        assertTrue(result.option.remaining)
        assertEquals(1, result.tolls.size)
        assertEquals(2500, result.tolls.single().lowCents)
        assertTrue(result.tolls.single().freeFlow)
        assertEquals(listOf(GOOGLE_ROUTE, ROUTE), client.list(trip).options.map { it.id })
        client.attach(trip, result.option)
        assertEquals(result, client.list(trip).linked)
        assertEquals(64.3, trip.distanceKm)
        assertEquals(25.0, trip.fuelLiters)
    }

    @Test fun comparesIndependentTerrainForecastsAndKeepsCostsIncompleteWhenNeeded() {
        fun plan(id: String, liters: Double, method: String, complete: Boolean) = route().put("id", id).put("revision", 2)
            .put("forecast", JSONObject().put("method", method)
                .put("tripId", TRIP).put("routeId", id).put("routeRevision", 2)
                .put("modelVersion", "terrain-test").put("confidence", "MEDIUM").put("historySamples", 140)
                .put("fuelLiters", liters)
                .put("cost", JSONObject().put("tollPayer", "CARRIER").put("fuelCents", 43_000)
                    .put("operatingCents", 43_000).put("complete", complete)))
        val fake = FakeCloud { _, request ->
            val payload = request.getJSONObject("payload")
            assertEquals(3, payload.getInt("alternatives"))
            assertEquals("CARRIER", payload.getString("tollPayer"))
            assertEquals("Cidade intermediária", payload.getJSONArray("waypoints").getString(0))
            val plans = JSONArray()
                .put(plan(ROUTE, 400.0, "TRUCK_TERRAIN_HISTORY_V1", false))
                .put(plan(GOOGLE_ROUTE, 350.0, "TRUCK_TERRAIN_HISTORY_V1", true).put("distanceM", 815_000))
                .put(plan(VEHICLE, 985.0, "ACTIVE_TRIP_AVERAGE", true))
            200 to JSONObject().put("routes", plans)
                .put("appliedWaypoints", JSONArray().put("Cidade intermediária")).toString()
        }
        val client = TripRoutesClient(prefs(context), fake.transport())
        val results = client.calculate(trip.copy(averageKml = .8), TruckRouteProfile(420, 260, 74_000, 72_000),
            true, listOf("Cidade intermediária"), "CARRIER")
        assertEquals(3, results.size)
        assertEquals(400.0, results[0].forecast?.liters)
        assertEquals(350.0, results[1].forecast?.liters)
        assertFalse(results[0].forecast!!.operatingCostComplete)
        assertTrue(results[1].forecast!!.operatingCostComplete)
        assertNull(results[2].forecast)
    }

    @Test fun requestedWaypointsAreNotSilentlyDiscardedByTheLegacyApi() {
        val fake = FakeCloud { _, _ -> 200 to JSONObject().put("route", route()).toString() }
        val client = TripRoutesClient(prefs(context), fake.transport())
        val profile = TruckRouteProfile(420, 260, 74_000, 72_000)
        assertEquals("waypoints_unconfirmed", assertThrows(DriverCloudException::class.java) {
            client.calculate(trip, profile, fromGps = false, waypoints = listOf("Cidade intermediária"))
        }.code)
    }

    @Test fun noGpsAndChangedTripProduceExplicitErrors() {
        val fake = FakeCloud { _, _ -> 422 to "{\"error\":\"fresh_gps_required\"}" }
        val client = TripRoutesClient(prefs(context), fake.transport())
        val profile = TruckRouteProfile(420, 260, 74_000, 72_000)
        assertEquals("fresh_gps_required", assertThrows(DriverCloudException::class.java) {
            client.calculate(trip, profile, fromGps = true)
        }.code)
        fake.reply = { _, _ -> 409 to "{\"error\":\"driver_trip_changed\"}" }
        assertEquals(409, assertThrows(DriverCloudException::class.java) {
            client.list(trip)
        }.status)
    }

    @Test fun localTripOrRouteFromOtherTripCannotBeUsed() {
        val fake = FakeCloud { _, _ -> 200 to JSONObject().put("route", route().put("tripId", VEHICLE))
            .put("plans", JSONArray()).toString() }
        val client = TripRoutesClient(prefs(context), fake.transport())
        assertThrows(IllegalArgumentException::class.java) { client.list(trip.copy(cloudId = null)) }
        assertTrue(fake.requests.isEmpty())
        assertThrows(IllegalArgumentException::class.java) { client.list(trip) }
    }

    @Test fun rejectsWeightAbovePbtcAndDecodesOnlyValidGeometry() {
        assertNull(TruckRouteProfile.fromInput("4,25", "2,60", "74", "75"))
        assertNull(TruckRouteProfile.fromInput("4,25", "2,60", "74", ""))
        assertEquals(listOf(-120.2 to 38.5, -120.95 to 40.7, -126.453 to 43.252), decodeRoutePolyline(POLYLINE))
        assertTrue(decodeRoutePolyline("????\\").isEmpty())
        assertTrue(decodeRoutePolyline(POLYLINE.dropLast(1)).isEmpty())
    }

    private fun option(id: String, provider: String) = JSONObject().put("id", id).put("provider", provider)
        .put("distanceM", 800_000).put("durationSec", 33_000).put("destination", "Destino")
    private fun route() = option(ROUTE, "HERE").put("tripId", TRIP).put("origin", "Posição do caminhão")
        .put("createdAt", "2026-11-20T10:00:00.321+00:00").put("validUntil", "2026-12-20T10:00:00.321+00:00")
        .put("polyline", POLYLINE).put("tollCoverage", "PARTIAL")
        .put("tolls", JSONArray().put(JSONObject().put("name", "Pórtico de teste").put("kind", "FREE_FLOW")
            .put("lowCents", 2500).put("highCents", 2800).put("atM", 270_200)))

    private companion object {
        const val POLYLINE = "_p~iF~ps|U_ulLnnqC_mqNvxq`@"
    }
}
