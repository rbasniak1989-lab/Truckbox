package br.com.truckbox.app

import android.location.Location
import br.com.truckbox.app.gps.PhoneGpsSample
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class PhoneGpsSampleTest {
    private val elapsed = 50_000_000_000L
    private fun location() = Location("gps").apply {
        time = TEST_TIME; elapsedRealtimeNanos = elapsed
        latitude = -25.0; longitude = -49.0
    }
    private fun capture(l: Location) = PhoneGpsSample.fromLocation(l, "installation", TEST_TIME, elapsed)
    @Test fun preservesRealCaptureTimeAndConvertsSpeed() {
        val l = location().apply { speed = 10f; accuracy = 7f; altitude = 900.0; bearing = 120f }
        val p = requireNotNull(capture(l))
        assertEquals(TEST_TIME, p.capturedAtMs); assertEquals(36.0, p.speedKmh!!, .001)
        assertEquals(7.0, p.accuracyM!!, .001); assertEquals("installation:$TEST_TIME", p.clientUid)
    }
    @Test fun unavailableOptionalFieldsStayNull() {
        val p = requireNotNull(capture(location()))
        assertNull(p.speedKmh); assertNull(p.altitudeM); assertNull(p.accuracyM); assertNull(p.headingDeg)
    }
    @Test fun staleFixIsRejected() { assertNull(capture(location().apply { elapsedRealtimeNanos = elapsed - 31_000_000_000L })) }
    @Test fun futureElapsedFixIsRejected() { assertNull(capture(location().apply { elapsedRealtimeNanos = elapsed + 1 })) }
    @Test fun futureOrMissingCaptureTimeIsRejected() {
        assertNull(capture(location().apply { time = TEST_TIME + 60_001 })); assertNull(capture(location().apply { time = 0 }))
    }
    @Test fun invalidCoordinatesOrAccuracyAreRejected() {
        assertNull(capture(location().apply { latitude = Double.NaN }))
        assertNull(capture(location().apply { longitude = 181.0 }))
        assertNull(capture(location().apply { accuracy = Float.NaN }))
    }
    @Test fun coarsePositionKeepsItsReportedAccuracy() {
        val p = capture(location().apply { provider = "network"; accuracy = 2000f })!!
        assertEquals(2000.0, p.accuracyM!!, 0.0); assertEquals("network", p.provider)
    }
    @Test fun mockPositionIsRejected() {
        val l = location(); l.isMock = true
        assertNull(capture(l))
    }
}
