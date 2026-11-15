package br.com.truckbox.app

import androidx.test.core.app.ApplicationProvider
import android.content.Context
import br.com.truckbox.app.cloud.DriverCloudException
import br.com.truckbox.app.gateway.GatewayCloudClient
import br.com.truckbox.app.gps.*
import org.junit.Assert.*
import org.junit.Before
import org.junit.After
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.IOException

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class DriverGpsSyncTest {
    private val context = ApplicationProvider.getApplicationContext<Context>()
    private lateinit var box: DriverGpsOutbox
    private val scope = TEST_CREDENTIALS.scope
    @Before fun setup() { context.deleteDatabase("truckbox_driver_gps_v1.db"); box = DriverGpsOutbox(context) }
    @After fun close() { box.close() }
    @Test fun persistsAndDeduplicatesAcrossProcessRecreation() {
        assertEquals(GpsEnqueue.INSERTED, box.enqueue(scope, sample()))
        box.close(); box = DriverGpsOutbox(context)
        assertEquals(GpsEnqueue.DUPLICATE, box.enqueue(scope, sample()))
        assertEquals(1, box.countAll()); assertEquals(sample(), box.next(scope)!!.sample)
    }
    @Test fun offlineFailureRetainsExactPointAndRetryUsesSameIdentity() {
        box.enqueue(scope, sample())
        val fake = FakeCloud { _, _ -> throw IOException("offline") }
        val sync = DriverGpsSync(box, GatewayCloudClient(fake.transport()))
        assertThrows(IOException::class.java) { sync.drain(TEST_CREDENTIALS, { true }) }
        assertEquals(1, box.countAll())
        fake.reply = { _, body -> gpsAck(body) }
        assertEquals(1, sync.drain(TEST_CREDENTIALS, { true })); assertEquals(0, box.countAll())
        assertEquals(fake.requests[0].second.toString(), fake.requests[1].second.toString())
    }
    @Test fun replaySendsNewestFirstThenOlderHistoryAndDoesNotRequireCurrentFix() {
        listOf(0L, 1000L, 2000L).forEach { box.enqueue(scope, sample(TEST_TIME + it)) }
        val fake = FakeCloud { _, body -> gpsAck(body) }
        assertEquals(3, DriverGpsSync(box, GatewayCloudClient(fake.transport())).drain(TEST_CREDENTIALS, { true }))
        assertEquals(listOf(TEST_TIME + 2000, TEST_TIME, TEST_TIME + 1000), fake.requests.map { br.com.truckbox.app.cloud.cloudInstant(it.second.getString("ts")).toEpochMilli() })
    }
    @Test fun wrongTruckOrTimestampCannotAcknowledgePoint() {
        box.enqueue(scope, sample())
        val fake = FakeCloud { _, body -> gpsAck(body, vehicle = "wrong-vehicle") }
        val sync = DriverGpsSync(box, GatewayCloudClient(fake.transport()))
        assertThrows(DriverCloudException::class.java) { sync.drain(TEST_CREDENTIALS, { true }) }
        fake.reply = { _, body -> gpsAck(body, time = "2026-01-01T00:00:00Z") }
        assertThrows(DriverCloudException::class.java) { sync.drain(TEST_CREDENTIALS, { true }) }
        assertEquals(1, box.countAll())
    }
    @Test fun revokedCredentialKeepsQueuedPositions() {
        box.enqueue(scope, sample())
        val fake = FakeCloud { _, _ -> 403 to "private-server-error" }
        val error = assertThrows(DriverCloudException::class.java) { DriverGpsSync(box, GatewayCloudClient(fake.transport())).drain(TEST_CREDENTIALS, { true }) }
        assertEquals(403, error.status); assertFalse(error.message!!.contains("private")); assertEquals(1, box.countAll())
    }
    @Test fun disabledSharingSendsNothingAndPreservesQueue() {
        box.enqueue(scope, sample())
        val fake = FakeCloud { _, body -> gpsAck(body) }
        assertEquals(0, DriverGpsSync(box, GatewayCloudClient(fake.transport())).drain(TEST_CREDENTIALS, { false }))
        assertEquals(1, box.countAll()); assertTrue(fake.requests.isEmpty())
    }
    @Test fun anotherDeviceQueueIsNeverSentOrDeleted() {
        box.enqueue("other-device|other-vehicle", sample())
        val fake = FakeCloud { _, body -> gpsAck(body) }
        assertEquals(0, DriverGpsSync(box, GatewayCloudClient(fake.transport())).drain(TEST_CREDENTIALS, { true }))
        assertEquals(1, box.countAll())
    }
    @Test fun capacityDoesNotDeleteUnacknowledgedHistory() {
        box.close(); box = DriverGpsOutbox(context, limit = 1)
        box.enqueue(scope, sample())
        assertEquals(GpsEnqueue.FULL, box.enqueue(scope, sample(TEST_TIME + 1000)))
        assertEquals(sample(), box.next(scope)!!.sample)
    }
    @Test fun payloadHasRealSourceAndNullUnknownFields() {
        box.enqueue(scope, sample())
        val fake = FakeCloud { url, body ->
            assertEquals("https", url.protocol); assertTrue(url.path.endsWith("/truckbox-gps"))
            assertEquals("ANDROID_PHONE", body.getString("source")); assertEquals(-25.0, body.getDouble("lat"), 0.0)
            assertFalse(body.has("speed_kmh")); assertFalse(body.has("altitude_m")); gpsAck(body)
        }
        DriverGpsSync(box, GatewayCloudClient(fake.transport())).drain(TEST_CREDENTIALS, { true })
        assertFalse(fake.connections.single().instanceFollowRedirects)
    }
}
