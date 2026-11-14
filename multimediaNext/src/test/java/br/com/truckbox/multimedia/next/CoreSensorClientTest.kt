package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.data.*
import br.com.truckbox.multimedia.next.domain.*
import br.com.truckbox.multimedia.next.ui.visibleValue
import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.net.HttpURLConnection
import java.net.URL
import java.time.Instant

/** Fixtures locais do contrato publicado pelo Core; não são leituras do caminhão do usuário. */
class CoreSensorClientTest {
    private var now=Instant.parse("2026-11-14T12:00:00Z")
    private class Reply(url: URL,private val body: String,private val code: Int=200): HttpURLConnection(url) {
        override fun connect() {}
        override fun disconnect() {}
        override fun usingProxy()=false
        override fun getResponseCode()=code
        override fun getInputStream()=ByteArrayInputStream(body.toByteArray())
    }
    private fun row(source: String="j1939",id: Int=34,value: String="4.4",unit: String="bar",quality: String="VALIDO",validation: String="VALIDADO_NOS_LOGS",age: Long=20,boot: Long=1,address: Int=0,pgn: Int=65263,capture: Long=100): String =
        """{"source":"$source",${if(source=="j1939") "\"sid\":$id,\"sa\":$address,\"pgn\":$pgn" else "\"pid\":$id,\"mid\":$address"},"unit":"$unit","quality":"$quality","validation":"$validation","capture_id":$capture,"boot_id":$boot,"uptime_us":100000,"age_ms":$age,"stale":${age>5000},"value":$value,"last_valid_value":999}"""
    private fun page(rows: List<String>,offset: Int=0,total: Int=rows.size,next: Int=offset+rows.size)=
        """{"ok":true,"schema_version":2,"offset":$offset,"count":${rows.size},"observed_count":$total,"next_offset":$next,"readings":[${rows.joinToString(",")}]}"""
    private fun client(body: (URL)->String)=CoreSensorClient(CoreConnectionFactory {url->Reply(url,
        if(url.path=="/api/health") """{"ok":true,"role":"core","device_uid":"test-core","boot_id":1}""" else body(url))},now={now},monotonic={0})

    @Test fun pagesObservedReadingsWithMetadataAndExactUnits() {
        val paths=mutableListOf<URL>()
        val result=client {url->
            paths+=url
            if(url.query.contains("offset=0")) page(listOf(row()),total=2)
            else page(listOf(row(id=24,value="87",unit="degC",pgn=65262,capture=101)),offset=1,total=2)
        }.fetch("192.168.4.1")
        assertEquals(2,paths.size)
        assertTrue(paths.all {it.path=="/api/sensors" && it.query.contains("limit=8")})
        val oil=result.response!!.values[Instrument.OIL_PRESSURE]!!.first
        assertEquals(4.4,oil.current(now)!!,0.001)
        assertEquals(now.minusMillis(20),oil.sampledAt)
        assertEquals(100L,oil.captureId)
        assertEquals(1L,oil.bootId)
        assertEquals("J1939/SA:0/PGN:65263/SID:34",oil.sourceId)
        assertEquals(87.0,result.response!!.values[Instrument.COOLANT]!!.first.current(now)!!,0.01)
        assertEquals(2,result.diagnostics.validValues)
        assertEquals("test-core",result.response!!.physicalDeviceUid)
    }
    @Test fun j1587ObservationIsVisibleWithProvisionalQualityButCannotAuthorizeCalculations() {
        val result=client {page(listOf(row(source="j1587",id=100,address=128,quality="EM_VALIDACAO",validation="EM_VALIDACAO")))}.fetch("truckbox.local")
        val reading=result.response!!.values[Instrument.OIL_PRESSURE]!!.first
        assertEquals(Quality.PROVISIONAL,reading.quality)
        assertEquals(4.4,visibleValue(reading,now,false)!!,0.01)
        assertNull(reading.current(now))
        assertEquals(1,result.diagnostics.provisionalValues)
    }
    @Test fun staleAndInvalidSamplesNeverUseLastValidAsCurrent() {
        for(sample in listOf(row(age=5001),row(value="null",quality="INDISPONIVEL"),row(quality="ERRO_SAE"))) {
            val result=client {page(listOf(sample))}.fetch("truckbox.local")
            val reading=result.response!!.values[Instrument.OIL_PRESSURE]!!.first
            assertNull(visibleValue(reading,now,false))
            assertNotEquals(999.0,reading.value)
        }
    }
    @Test fun replayOfSameCaptureDoesNotRefreshItsTimestamp() {
        val c=client {page(listOf(row(age=0)))}
        val first=c.fetch("truckbox.local").response!!.values[Instrument.OIL_PRESSURE]!!.first
        now=now.plusSeconds(6)
        val replay=c.fetch("truckbox.local").response!!.values[Instrument.OIL_PRESSURE]!!.first
        assertEquals(first.sampledAt,replay.sampledAt)
        assertNull(replay.current(now))
    }
    @Test fun conflictingReplayDiscardsTheSnapshot() {
        var changed=false
        val c=client {page(listOf(row(value=if(changed) "8.8" else "4.4")))}
        assertNotNull(c.fetch("truckbox.local").response)
        changed=true
        val result=c.fetch("truckbox.local")
        assertNull(result.response)
        assertTrue(result.diagnostics.attempts.all {it.issue==CoreIssue.FORMAT})
    }
    @Test fun rebootBetweenPagesDoesNotMixEpochs() {
        val result=client {url->if(url.query.contains("offset=0")) page(listOf(row()),total=2)
            else page(listOf(row(boot=2,id=24,unit="degC",pgn=65262)),offset=1,total=2)}.fetch("truckbox.local")
        assertNull(result.response)
        assertTrue(result.diagnostics.attempts.all {it.issue==CoreIssue.FORMAT})
    }
    @Test fun paginationIsBoundedAndRequiresForwardProgress() {
        for(body in listOf(page(listOf(row()),total=193),page(emptyList(),total=8),page(listOf(row()),total=8,next=0))) {
            var calls=0
            val result=client {calls++;body}.fetch("truckbox.local")
            assertNull(result.response)
            assertEquals(2,calls)
        }
    }
    @Test fun signalIdentityAndUnitsCannotBeReplacedByLoadRangeOrUnknownFormula() {
        val result=client {page(listOf(row(pgn=123),row(unit="psi"),row(source="j1587",id=92,unit="%",address=128),row(source="j1587",id=163,unit="range",address=130)))}.fetch("truckbox.local")
        assertTrue(result.response!!.values.isEmpty())
        assertEquals(0,result.diagnostics.recognizedValues)
    }
    @Test fun bothAirCircuitsStayInOneCardAndRequireTheSameMid() {
        fun air(mid: Int,id: Int)=row(source="j1587",id=id,address=mid,quality="EM_VALIDACAO",validation="EM_VALIDACAO",value="10.1")
        val pair=client {page(listOf(air(140,117),air(140,118)))}.fetch("truckbox.local").response!!.values
        assertEquals(setOf(Instrument.AIR),pair.keys)
        assertNotNull(pair[Instrument.AIR]!!.first.observed(now))
        assertNotNull(pair[Instrument.AIR]!!.second!!.observed(now))
        val mismatch=client {page(listOf(air(140,117),air(128,118)))}.fetch("truckbox.local").response!!.values
        assertFalse(mismatch.containsKey(Instrument.AIR))
    }
    @Test fun emptySensorCacheDoesNotManufactureZeroValues() {
        val result=client {page(emptyList())}.fetch("truckbox.local")
        assertTrue(result.diagnostics.responded)
        assertTrue(result.response!!.values.isEmpty())
        assertNull(result.response!!.speed)
        assertNull(result.response!!.flow)
    }
    @Test fun cloudBindingChecksCoreUidBeforeReadingAnySensor() {
        val urls=mutableListOf<URL>()
        val result=CoreSensorClient(CoreConnectionFactory {url->
            urls+=url
            Reply(url,if(url.path=="/api/health") """{"ok":true,"role":"core","device_uid":"other-core","boot_id":1}""" else page(listOf(row())))
        },now={now},monotonic={0}).fetch("truckbox.local","expected-core")
        assertNull(result.response)
        assertTrue(urls.isNotEmpty())
        assertTrue(urls.all {it.path=="/api/health"})
        assertTrue(result.diagnostics.attempts.all {it.issue==CoreIssue.IDENTITY})
    }
    @Test fun replacingThePhysicalCoreDoesNotReuseThePreviousCaptureCache() {
        var uid="first-core";var number="4.4"
        val c=CoreSensorClient(CoreConnectionFactory {url->Reply(url,
            if(url.path=="/api/health") """{"ok":true,"role":"core","device_uid":"$uid","boot_id":1}""" else page(listOf(row(value=number))))},now={now},monotonic={0})
        assertEquals("first-core",c.fetch("192.168.4.1").response!!.physicalDeviceUid)
        uid="second-core";number="5.5"
        val next=c.fetch("192.168.4.1").response!!
        assertEquals("second-core",next.physicalDeviceUid)
        assertEquals(5.5,next.values[Instrument.OIL_PRESSURE]!!.first.value!!,0.0)
    }
}
