package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.data.CoreConnectionFactory
import br.com.truckbox.multimedia.next.data.CoreFaultClient
import br.com.truckbox.multimedia.next.data.CoreLiveClient
import br.com.truckbox.multimedia.next.domain.Instrument
import br.com.truckbox.multimedia.next.domain.Quality
import br.com.truckbox.multimedia.next.domain.confirmedActive
import br.com.truckbox.multimedia.next.ui.instrumentDisplayValue
import br.com.truckbox.multimedia.next.ui.instrumentText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.net.HttpURLConnection
import java.net.URL
import java.time.Instant

class CoreLiteHomeSignalsTest {
    private val now = Instant.parse("2026-11-22T17:35:12.528Z")
    private val uid = "test-core-lite"
    private val host = "192.168.4.1"

    private fun replies(body: (URL) -> String) = CoreConnectionFactory { url ->
        object : HttpURLConnection(url) {
            override fun connect() = Unit
            override fun disconnect() = Unit
            override fun usingProxy() = false
            override fun getResponseCode() = 200
            override fun getInputStream() = ByteArrayInputStream(body(url).toByteArray())
        }
    }

    @Test fun localGearAndDerivedTorqueUseTheLiteContract() {
        val client = CoreLiveClient(replies { url ->
            assertEquals("/api/live", url.path)
            """{"device_uid":"$uid","boot_id":2,"live_contract":"TBX_CORE_LIVE_V1",
                "gear":1,"actualTorquePct":2.0507,"live_readings":{
                  "gear":{"mid":130,"pid":163,"age_ms":500,"quality":"EM_VALIDACAO"},
                  "torque":{"mid":128,"pid":93,"age_ms":500,"reference_nm":2643,
                    "quality":"DERIVADO_REFERENCIA_HISTORICA_EM_VALIDACAO"}
                }}"""
        }, now = { now })
        val readings = client.fetch(host, uid)
        val gear = readings.getValue(Instrument.GEAR).first
        val torque = readings.getValue(Instrument.TORQUE).first
        assertEquals(1.0, gear.value!!, 0.0)
        assertEquals("J1587/MID:130/PID:163", gear.sourceId)
        assertEquals(2.0507, torque.value!!, 0.0001)
        assertEquals("J1587/MID:128/PID:93", torque.sourceId)
        assertEquals(Quality.PROVISIONAL, torque.quality)
        assertTrue(client.fetch(host, "different-core").isEmpty())
    }

    @Test fun neutralFromTheLocalCoreReplacesGearAtAnySpeed() {
        var gear: Int? = 4
        var label = "4"
        var speed = 0
        var uidTime = 100L
        val client = CoreLiveClient(replies {
            """{"device_uid":"$uid","boot_id":2,"live_contract":"TBX_CORE_LIVE_V1","speedKph":$speed,
                "gear":$gear,"gear_text":"$label","live_sample_v1":{"gear_uptime_ms":$uidTime},
                "live_readings":{"gear":{"mid":130,"pid":163,"age_ms":100,"quality":"EM_VALIDACAO"}}}"""
        }, now = { now }, monotonic = { 0L })
        fun shown(): String {
            val reading = client.fetch(host, uid)[Instrument.GEAR]?.first
            return instrumentText(Instrument.GEAR, reading, instrumentDisplayValue(Instrument.GEAR, reading, now, false))
        }
        assertEquals("4", shown())
        gear = 0; label = "N"; uidTime++
        assertEquals("N", shown())
        speed = 30; uidTime++
        assertEquals("N", shown())
        gear = null; label = ""; uidTime++
        assertEquals("—", shown())
        gear = 0; label = "0"; uidTime++
        assertEquals("—", shown())
        gear = -2; label = "R2"; uidTime++
        assertEquals("R2", shown())
        gear = 3; label = "3"; uidTime++
        assertEquals("3", shown())
    }

    @Test fun repeatedTorqueTimestampCannotRefreshTheSample() {
        var clock = now
        var stamp = 9_500L
        var age = 0
        val client = CoreLiveClient(replies {
            """{"device_uid":"$uid","boot_id":2,"live_contract":"TBX_CORE_LIVE_V1",
                "torqueNm":-27.1,"live_sample_v1":{"torque_uptime_ms":$stamp},
                "live_readings":{"torque":{"mid":128,"pid":93,"age_ms":$age,"quality":"EM_VALIDACAO"}}}"""
        }, now = { clock }, monotonic = { 0L })
        val reading = client.fetch(host, uid).getValue(Instrument.TORQUE).first
        assertEquals(-27.1, reading.value!!, 0.0)
        assertEquals("N·m", reading.unit)
        assertEquals(stamp, reading.sampleUptimeMs!!)
        clock = now.plusMillis(2400)
        assertEquals(now, client.fetch(host, uid).getValue(Instrument.TORQUE).first.sampledAt)
        clock = now.plusMillis(2600)
        assertNull(instrumentDisplayValue(Instrument.TORQUE, reading, clock, false))
        assertNull(client.fetch(host, uid)[Instrument.TORQUE])
        stamp += 1000
        assertEquals(stamp, client.fetch(host, uid).getValue(Instrument.TORQUE).first.sampleUptimeMs!!)
        age = 2800
        assertNull(client.fetch(host, uid)[Instrument.TORQUE])
    }

    @Test fun liteDiagnosticsCanReportAnActiveDtcWithoutPersistentHistory() {
        val requested = mutableListOf<Int>()
        val client = CoreFaultClient(replies { url ->
            assertEquals("/api/diagnostics", url.path)
            val offset = url.query.substringAfter("offset=").substringBefore('&').toInt()
            requested += offset
            val faults = if (offset == 0) """{"mid":136,"identifier_type":"SID","identifier":2,
                "fmi":5,"state":"ACTIVE","occurrences":1,"quality":"SAE_PARSED_OEM_UNCONFIRMED",
                "last":[2,1000,${now.minusSeconds(1).toEpochMilli()}]}""" else ""
            """{"ok":true,"device_uid":"$uid","offset":$offset,"next_offset":${offset + 8},
                "capacity":32,"overflow":0,"persistent_ready":false,"faults":[$faults]}"""
        }, now = { now })
        val snapshot = client.fetch(host, uid)
        assertNotNull(snapshot)
        assertEquals(listOf(0, 8, 16, 24), requested)
        val fault = snapshot!!.faults.single()
        assertTrue(snapshot.incomplete)
        assertEquals("MID 136 • SID 2 • FMI 5", fault.technicalCode)
        assertTrue(fault.confirmedActive(now, now, true))
        assertFalse(fault.confirmedActive(now, now, false))
    }
}
