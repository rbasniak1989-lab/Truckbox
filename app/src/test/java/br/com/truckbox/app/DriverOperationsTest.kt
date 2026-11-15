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
    @Test fun staleSnapshotCannotReopenAnOfflineEndedTrip() {
        start(); val uid = store.state.value.activeTrip!!.clientUid; store.endActiveTrip(null, null)
        store.applyCloudSnapshot(emptySnapshot().put("trips", JSONArray().put(cloudTrip(uid))))
        assertNull(store.state.value.activeTrip)
        assertEquals("COMPLETED", store.state.value.trips.single().status)
        assertNull(store.state.value.emptyFuelLiters)
    }
    @Test fun reconnectSendsCommandsInOrderAndOnlyAcknowledgesExactRecords() = runBlocking {
        start(); store.endActiveTrip(null, null); start("Segunda origem")
        val fake = FakeCloud { _, body -> if (body.getString("action") == "snapshot") 200 to emptySnapshot().toString() else ack(body) }
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertEquals(listOf("upsert_trip", "end_trip", "upsert_trip", "snapshot"), fake.requests.map { it.second.getString("action") })
        assertTrue(store.state.value.pendingCommands.isEmpty()); assertTrue(GatewayStatus.state.value.operationsCloudOnline)
        val payload = fake.requests.first().second.getJSONObject("payload")
        assertTrue(payload.isNull("start_core_fuel_liters")); assertTrue(payload.isNull("start_odometer_km"))
    }
    @Test fun partialFailureKeepsUnconfirmedCommandsForRetry() = runBlocking {
        start(); store.endActiveTrip(null, null)
        var calls = 0
        val fake = FakeCloud { _, body -> if (++calls == 2) throw IOException("offline") else ack(body) }
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertEquals(DriverAction.END_TRIP, store.state.value.pendingCommands.single().action)
        val savedUid = store.state.value.pendingCommands.single().trip!!.clientUid
        fake.reply = { _, body -> if (body.getString("action") == "snapshot") 200 to emptySnapshot().toString() else ack(body) }
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertTrue(store.state.value.pendingCommands.isEmpty())
        assertEquals(savedUid, fake.requests[2].second.getJSONObject("payload").getString("client_uid"))
    }
    @Test fun wrongAcknowledgmentCannotRemoveOrSkipTheHeadCommand() = runBlocking {
        start(); store.endActiveTrip(null, null)
        val fake = FakeCloud { _, _ -> 200 to "{\"ok\":true,\"item\":{\"client_uid\":\"wrong\",\"truck_id\":\"$VEHICLE\"}}" }
        DriverOperationsSync.sync(preferences, store, fake.transport())
        assertEquals(2, store.state.value.pendingCommands.size); assertEquals(1, fake.requests.size)
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
}
