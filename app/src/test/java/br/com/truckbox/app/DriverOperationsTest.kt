package br.com.truckbox.app

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import br.com.truckbox.app.cloud.*
import br.com.truckbox.app.gateway.GatewayStatus
import br.com.truckbox.app.operations.*
import br.com.truckbox.app.preferences.TruckBoxPreferences
import kotlinx.coroutines.runBlocking
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.IOException

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class DriverOperationsTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private lateinit var preferences: TruckBoxPreferences
    private lateinit var store: OperationsStore
    @Before fun setup() {
        context.getSharedPreferences("truckbox", 0).edit().clear().commit()
        context.getSharedPreferences("truckbox_operations_v041", 0).edit().clear().commit()
        preferences = prefs(context); store = OperationsStore.isolatedForTest(context, preferences)
    }
    private fun start(origin: String = "Origem") = store.startTrip(origin, "Destino", 20.0, 100.0, null, null)
    private fun emptySnapshot() = JSONObject().put("ok", true).put("trips", JSONArray())
        .put("fuelings", JSONArray()).put("expenses", JSONArray()).put("maintenance", JSONArray())
    private fun cloudTrip(uid: String, status: String = "ACTIVE") = JSONObject().put("client_uid", uid)
        .put("truck_id", VEHICLE).put("origin", "Origem").put("destination", "Destino")
        .put("started_at", "2026-11-14T12:00:00.123+00:00").put("status", status)
    private fun ack(body: JSONObject) = 200 to JSONObject().put("ok", true).put("item", JSONObject()
        .put("client_uid", body.getJSONObject("payload").getString("client_uid")).put("truck_id", VEHICLE)).toString()
    @Test fun offlineStartEndStartIsDurableAndImmutable() {
        start(); val firstUid = store.state.value.activeTrip!!.clientUid
        store.endActiveTrip(null, null); start("Segunda origem")
        store = OperationsStore.isolatedForTest(context, preferences)
        val q = store.state.value.pendingCommands
        assertEquals(listOf(DriverAction.UPSERT_TRIP, DriverAction.END_TRIP, DriverAction.UPSERT_TRIP), q.map { it.action })
        assertEquals(firstUid, q[0].trip!!.clientUid); assertEquals("ACTIVE", q[0].trip!!.status)
        assertEquals("COMPLETED", q[1].trip!!.status)
        assertEquals("Segunda origem", store.state.value.activeTrip!!.origin)
        assertNull(q[0].trip!!.startFuelCounterL); assertNull(q[1].trip!!.fuelLiters)
    }
    @Test fun officialActiveTripIsShownUntilTheQueuedEndIsConfirmed() {
        start(); val uid = store.state.value.activeTrip!!.clientUid; store.endActiveTrip(null, null)
        store.applyCloudSnapshot(emptySnapshot().put("trips", JSONArray().put(cloudTrip(uid))))
        assertEquals(uid, store.state.value.activeTrip!!.clientUid)
        assertEquals("COMPLETED", store.state.value.pendingCommands.last().trip!!.status)
        assertNull(store.state.value.emptyFuelLiters)
    }
    @Test fun reconnectSendsCommandsInOrderAndOnlyAcknowledgesExactRecords() = runBlocking {
        start(); store.endActiveTrip(null, null); start("Segunda origem")
        val fake = replayCloud()
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertEquals(listOf("snapshot", "upsert_trip", "snapshot", "end_trip", "snapshot", "upsert_trip", "snapshot"),
            fake.requests.map { it.second.getString("action") })
        assertTrue(store.state.value.pendingCommands.isEmpty()); assertTrue(GatewayStatus.state.value.operationsCloudOnline)
        val payload = fake.requests[1].second.getJSONObject("payload")
        assertTrue(payload.isNull("start_core_fuel_liters")); assertTrue(payload.isNull("start_odometer_km"))
    }
    @Test fun partialFailureKeepsUnconfirmedCommandsForRetry() = runBlocking {
        start(); store.endActiveTrip(null, null)
        var failEnd = true
        val fake = replayCloud { action -> if (action == "end_trip" && failEnd) throw IOException("offline") }
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertEquals(DriverAction.END_TRIP, store.state.value.pendingCommands.single().action)
        val savedUid = store.state.value.pendingCommands.single().trip!!.clientUid
        failEnd = false
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertTrue(store.state.value.pendingCommands.isEmpty())
        assertEquals(savedUid, fake.requests.last { it.second.getString("action") == "end_trip" }
            .second.getJSONObject("payload").getString("client_uid"))
    }
    @Test fun wrongAcknowledgmentCannotRemoveOrSkipTheHeadCommand() = runBlocking {
        start(); store.endActiveTrip(null, null)
        val fake = FakeCloud { _, body -> if (body.getString("action") == "snapshot") 200 to emptySnapshot().toString()
            else 200 to "{\"ok\":true,\"item\":{\"client_uid\":\"wrong\",\"truck_id\":\"$VEHICLE\"}}" }
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertEquals(2, store.state.value.pendingCommands.size); assertEquals(2, fake.requests.size)
    }
    @Test fun externalActiveTripSupersedesLocalStartWithoutOverwritingIt() = runBlocking {
        start("Início offline")
        val local = store.state.value.activeTrip!!
        val tripId = "00000000-0000-4000-8000-000000000123"
        val external = cloudTrip("external").put("id", tripId).put("origin", "Origem remota")
            .put("weight_t", 36).put("rate_per_t", "175.00").put("freight_total", "6300.00")
            .put("start_odometer_km", 800_200.5).put("distance_km", 91.2).put("fuel_liters", 40.2)
            .put("empty_before_distance_km", 73.3).put("empty_before_fuel_liters", 21.5)
        var snapshot = emptySnapshot().put("trips", JSONArray().put(external)).put("operation", JSONObject()
            .put("truck_id", VEHICLE).put("mode", "LOADED_TRIP").put("active_trip_id", tripId))
        val fake = FakeCloud { _, body ->
            assertEquals("snapshot", body.getString("action"))
            200 to snapshot.toString()
        }
        DriverOperationsSync.sync(preferences, store, fake.transport())
        val active = store.state.value.activeTrip!!
        assertEquals("external", active.clientUid)
        assertEquals(tripId, active.cloudId)
        assertEquals(6300.0, active.freightTotal)
        assertEquals(800_200.5, active.startOdometerKm)
        assertEquals(91.2, active.distanceKm)
        assertEquals(40.2, active.fuelLiters)
        assertEquals(73.3, active.emptyBeforeDistanceKm)
        assertEquals(21.5, active.emptyBeforeFuelLiters)
        assertEquals(local.clientUid, store.state.value.pendingCommands.single().entityUid)
        assertTrue(store.state.value.pendingCommands.single().awaitingTripReview)

        store = OperationsStore.isolatedForTest(context, preferences)
        assertEquals(tripId, store.state.value.activeTrip?.cloudId)
        snapshot = emptySnapshot().put("operation", JSONObject().put("truck_id", VEHICLE).put("mode", "EMPTY_MODE"))
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertNull(store.state.value.activeTrip)
        assertTrue(store.state.value.pendingCommands.single().awaitingTripReview)
    }
    @Test fun acknowledgedCloudStartWinsAfterALostResponse() = runBlocking {
        start()
        val id = store.state.value.activeTrip!!.clientUid
        val fake = FakeCloud { _, body ->
            assertEquals("snapshot", body.getString("action"))
            200 to emptySnapshot().put("trips", JSONArray().put(cloudTrip(id).put("origin", "Confirmada na Cloud"))).toString()
        }
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertEquals("Confirmada na Cloud", store.state.value.activeTrip!!.origin)
        assertTrue(store.state.value.pendingCommands.isEmpty())
    }
    @Test fun snapshotForAnotherVehicleIsRejected() = runBlocking {
        val fake = FakeCloud { _, _ -> 200 to emptySnapshot().put("trips", JSONArray().put(cloudTrip("other").put("truck_id", "wrong"))).toString() }
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertTrue(store.state.value.trips.isEmpty()); assertFalse(GatewayStatus.state.value.operationsCloudOnline)
    }
    @Test fun missingServerTimeDoesNotBecomeCurrentTime() {
        assertThrows(Exception::class.java) {
            store.applyCloudSnapshot(emptySnapshot().put("trips", JSONArray().put(cloudTrip("x").apply { remove("started_at") })))
        }
        assertTrue(store.state.value.trips.isEmpty())
    }
    @Test fun postgresOffsetDatesWorkOnAndroid13() {
        store.applyCloudSnapshot(emptySnapshot().put("trips", JSONArray().put(cloudTrip("cloud"))))
        assertEquals(cloudInstant("2026-11-14T12:00:00.123Z").toEpochMilli(), store.state.value.activeTrip!!.startedAtMs)
    }
    @Test fun commandsWithoutConfigurationNeedExplicitBinding() {
        context.getSharedPreferences("truckbox", 0).edit().clear().commit()
        store = OperationsStore.isolatedForTest(context, TruckBoxPreferences(context, MemoryTokens()))
        start(); assertNull(store.state.value.pendingCommands.single().scope)
        store.bindUnassignedCommands(TEST_CREDENTIALS.scope)
        assertEquals(TEST_CREDENTIALS.scope, store.state.value.pendingCommands.single().scope)
    }

    private fun replayCloud(before: (String) -> Unit = {}): FakeCloud {
        val records = linkedMapOf<String, JSONObject>()
        return FakeCloud { _, body ->
            val action = body.getString("action")
            before(action)
            if (action == "snapshot") {
                200 to emptySnapshot().put("trips", JSONArray(records.values.toList())).toString()
            } else {
                val uid = body.getJSONObject("payload").getString("client_uid")
                records[uid] = cloudTrip(uid, if (action == "end_trip") "COMPLETED" else "ACTIVE")
                ack(body)
            }
        }
    }
}
