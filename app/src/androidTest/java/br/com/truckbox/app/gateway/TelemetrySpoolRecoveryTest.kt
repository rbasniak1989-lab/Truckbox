package br.com.truckbox.app.gateway

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Execute em um emulador: :app:connectedDebugAndroidTest.
 * Os UIDs sao exclusivos dos testes; nenhuma credencial Cloud e utilizada.
 */
@RunWith(AndroidJUnit4::class)
class TelemetrySpoolRecoveryTest {
    private val context: Context get() = ApplicationProvider.getApplicationContext()

    private fun uid() = "android-test-" + System.nanoTime().toString()

    @Test
    fun malformedSampleIsDurablyQuarantinedAndDoesNotLoseValidSibling() {
        val spool = TelemetrySpool(context)
        val before = spool.stats()
        val deviceUid = uid()
        val seq = (System.currentTimeMillis() * 1000L)
        val valid = JSONObject().put("seq", seq).put("rpm", 840)
        val malformed = JSONObject.NULL
        val batch = GatewayBatch(
            deviceUid = deviceUid,
            samples = JSONArray().put(valid).put(malformed),
            count = 2,
            maxSeq = seq + 1,
            pendingBytes = 350L,
            lastAckSeq = seq - 1,
        )

        val saved = spool.persist(batch, deviceUid)
        assertEquals(before.quarantinedBatches + 1, saved.quarantinedBatches)
        assertEquals(before.quarantinedSamples + 2, saved.quarantinedSamples)
        assertTrue(saved.bytes > before.bytes)
        assertTrue(saved.count > before.count)

        // Se o ACK local falhar e o ESP32 reenviar, nao duplica bytes/linhas.
        val replay = spool.persist(batch, deviceUid)
        assertEquals(saved.bytes, replay.bytes)
        assertEquals(saved.count, replay.count)
        assertEquals(saved.quarantinedBatches, replay.quarantinedBatches)
    }

    @Test
    fun stringifiedJSONObjectIsNormalizedWithoutQuarantine() {
        val spool = TelemetrySpool(context)
        val before = spool.stats()
        val deviceUid = uid()
        val seq = System.currentTimeMillis() * 1000L + 55
        val stringified = JSONObject().put("seq", seq).put("rpm", 650).toString()
        val batch = GatewayBatch(
            deviceUid = deviceUid,
            samples = JSONArray().put(stringified),
            count = 1,
            maxSeq = seq,
            pendingBytes = 120L,
            lastAckSeq = seq - 1,
        )
        val after = spool.persist(batch, deviceUid)
        assertEquals(before.quarantinedBatches, after.quarantinedBatches)
        assertEquals(before.count + 1, after.count)
    }

    @Test
    fun mismatchedDeviceUidCannotBeAcknowledgedByPersistence() {
        val spool = TelemetrySpool(context)
        val before = spool.stats()
        val seq = System.currentTimeMillis() * 1000L + 99
        val batch = GatewayBatch(
            deviceUid = uid(),
            samples = JSONArray().put(JSONObject().put("seq", seq)),
            count = 1,
            maxSeq = seq,
            pendingBytes = 50L,
            lastAckSeq = seq - 1,
        )
        try {
            spool.persist(batch, "different-device")
            throw AssertionError("Expected QueuePaused")
        } catch (_: TelemetrySpool.QueuePaused) {
            assertEquals(before.bytes, spool.stats().bytes)
            assertEquals(before.count, spool.stats().count)
        }
    }

    @Test
    fun inconsistentDeclaredCountIsBackedUpBeforeAcknowledgement() {
        val spool = TelemetrySpool(context)
        val before = spool.stats()
        val deviceUid = uid()
        val seq = System.currentTimeMillis() * 1000L + 123
        val batch = GatewayBatch(
            deviceUid = deviceUid,
            samples = JSONArray().put(JSONObject().put("seq", seq).put("rpm", 940)),
            count = 8,
            maxSeq = seq,
            pendingBytes = 200L,
            lastAckSeq = seq - 1,
        )
        val after = spool.persist(batch, deviceUid)
        assertEquals(before.quarantinedBatches + 1, after.quarantinedBatches)
        assertEquals(before.quarantinedSamples + 1, after.quarantinedSamples)
        assertTrue(after.quarantinedBytes > before.quarantinedBytes)
        assertEquals(before.count + 1, after.count)
    }
}
