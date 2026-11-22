package br.com.truckbox.multimedia.next.gps

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class GpsOutboxTest {
    @Test fun restartKeepsOrderAndAcknowledgementIsScopedToTheVehicle() {
        val context = RuntimeEnvironment.getApplication()
        val first = GpsPoint("point-a", 1795368912123, "{}")
        val second = GpsPoint("point-b", first.capturedAt + GPS_INTERVAL_MS, "{}")
        GpsOutbox(context).use { queue ->
            assertTrue(queue.enqueue("truck-a", first))
            assertTrue(queue.enqueue("truck-a", second))
            assertTrue(queue.enqueue("truck-a", first))
            assertTrue(queue.enqueue("truck-b", first))
        }
        GpsOutbox(context).use { queue ->
            assertEquals(2L, queue.pending("truck-a"))
            assertEquals(first, queue.oldest("truck-a"))
            queue.acknowledge("truck-a", first)
            assertEquals(second, queue.oldest("truck-a"))
            assertEquals(first, queue.oldest("truck-b"))
            queue.acknowledge("truck-a", second)
            assertNull(queue.oldest("truck-a"))
        }
    }
}
