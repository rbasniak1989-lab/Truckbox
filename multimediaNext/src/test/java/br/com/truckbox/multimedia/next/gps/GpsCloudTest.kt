package br.com.truckbox.multimedia.next.gps

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.time.Instant

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class GpsCloudTest {
    private val captured = Instant.parse("2026-11-22T17:30:00.123Z").toEpochMilli()

    private fun point(
        timestamp: Long = captured,
        now: Long = captured + 500,
        age: Long = 500,
        latitude: Double = -12.0,
        longitude: Double = -50.0,
        speed: Double? = null,
    ) = gpsPoint("test-install", timestamp, now, age, latitude, longitude, speed, null, null, null)

    @Test fun originalTimestampAndSourceAreKeptForReplay() {
        val fix = requireNotNull(point(speed = 10.0))
        val credentials = GpsCredentials("test-gps", "example-token")
        val saved = JSONObject(fix.json)
        val request = gpsUploadBody(credentials, fix)
        assertEquals("ANDROID_MULTIMEDIA", request.getString("source"))
        assertEquals("gps", request.getString("provider"))
        assertEquals("2026-11-22T17:30:00.123Z", request.getString("ts"))
        assertEquals(36.0, request.getDouble("speed_kmh"), 1e-9)
        assertEquals(fix.clientUid, point(now = captured + 5_000)?.clientUid)
        assertNotEquals(fix.clientUid, point(timestamp = captured + 100)?.clientUid)
        assertEquals("test-gps", request.getString("device_uid"))
        assertEquals("example-token", request.getString("token"))
        assertFalse(saved.has("token"))
        assertFalse(saved.has("device_uid"))
    }

    @Test fun missingMeasurementsStayMissing() {
        val fix = JSONObject(requireNotNull(point()).json)
        for (key in listOf("altitude_m", "speed_kmh", "heading_deg", "accuracy_m")) assertFalse(fix.has(key))
    }

    @Test fun rejectsStaleAndInvalidAndroidFixes() {
        assertNotNull(point())
        assertNull(point(age = 30_001))
        assertNull(point(age = -1))
        assertNull(point(now = captured - 5_001))
        assertNull(point(now = captured + 30_001))
        assertNull(point(latitude = 0.0, longitude = 0.0))
        assertNull(point(latitude = Double.NaN))
        assertNull(point(longitude = -180.01))
    }

    @Test fun ackMustConfirmThisFix() {
        val fix = requireNotNull(point())
        val ack = JSONObject().put("ok", true).put("truck_id", "test-truck")
            .put("accepted_ts", "2026-11-22T17:30:00.123Z")
        assertTrue(gpsAckMatches(ack.toString(), fix))
        assertFalse(gpsAckMatches(ack.put("accepted_ts", "2026-11-22T17:30:00Z").toString(), fix))
        assertFalse(gpsAckMatches("{\"ok\":true}", fix))
        assertFalse(gpsAckMatches("<!DOCTYPE html>", fix))
    }
}
