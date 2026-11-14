package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.data.*
import br.com.truckbox.multimedia.next.domain.*
import br.com.truckbox.multimedia.next.ui.instrumentDisplayValue
import org.junit.Assert.*
import org.junit.Test
import java.time.Instant

class CoreReadingsRetentionTest {
    private val at=Instant.parse("2026-11-14T12:00:00Z")
    private fun reading(value: Double?=38.0,quality: Quality=Quality.PROVISIONAL,time: Instant=at,source: String="J1587/MID:128/PID:105")=
        Reading(value,"°C",Origin.CORE,time,time,quality,source,100,1,"EM_VALIDACAO")
    private fun map(r: Reading)=mapOf(Instrument.INTAKE to InstrumentValue(r))

    @Test fun sparseNextResponseKeepsTheLastRealValueAndItsOriginalTime() {
        val original=reading()
        val held=retainCoreReadings(map(original),emptyMap())[Instrument.INTAKE]!!.first
        assertEquals(38.0,instrumentDisplayValue(held,at.plusSeconds(8),false)!!,0.0)
        assertEquals(original.sampledAt,held.sampledAt)
        assertEquals(Quality.STALE,held.quality)
        assertNull(held.current(at.plusSeconds(8)))
        assertNull(held.observed(at.plusSeconds(8)))
    }
    @Test fun missingAndInvalidValuesDoNotEraseTheLastUsableReading() {
        listOf(reading(null,Quality.MISSING),reading(0.0,Quality.INVALID),reading(Double.NaN)).forEach {bad->
            val held=retainCoreReadings(map(reading()),map(bad))[Instrument.INTAKE]!!.first
            assertEquals(38.0,held.value!!,0.0)
            assertEquals(Quality.STALE,held.quality)
        }
    }
    @Test fun aRealZeroAndTheNextValidCaptureReplaceTheHeldValue() {
        val zero=reading(0.0,time=at.plusSeconds(1)).copy(captureId=101)
        val result=retainCoreReadings(map(reading()),map(zero))[Instrument.INTAKE]!!.first
        assertEquals(zero,result)
        assertEquals(0.0,instrumentDisplayValue(result,at.plusSeconds(1),false)!!,0.0)
    }
    @Test fun anOlderOutOfOrderCaptureCannotRollTheDisplayBack() {
        val newer=reading(40.0,time=at.plusSeconds(1)).copy(captureId=101)
        val result=retainCoreReadings(map(newer),map(reading()))[Instrument.INTAKE]!!.first
        assertEquals(40.0,result.value!!,0.0)
        assertEquals(newer.sampledAt,result.sampledAt)
    }
    @Test fun aSensorNeverReadAndUnverifiedLegacyNumbersStayUnavailable() {
        assertTrue(retainCoreReadings(emptyMap(),emptyMap()).isEmpty())
        val invalid=reading(999.0,Quality.UNVERIFIED).copy(sampledAt=null,sourceId=null)
        val result=retainCoreReadings(emptyMap(),map(invalid))[Instrument.INTAKE]!!.first
        assertNull(instrumentDisplayValue(result,at,false))
        assertFalse(reading().copy(source=Origin.DEMO).canBeLastInstrumentValue())
        assertNull(instrumentDisplayValue(reading(time=at.plusSeconds(1)),at,false))
    }
    @Test fun offlineKeepsOnlyTheInstrumentSamplesWithoutRefreshingThem() {
        val held=staleCoreReadings(map(reading()))[Instrument.INTAKE]!!.first
        assertEquals(at,held.sampledAt)
        assertEquals(Quality.STALE,held.quality)
        assertNull(held.current(at))
        assertEquals(38.0,instrumentDisplayValue(held,at.plusSeconds(3600),false)!!,0.0)
    }
    @Test fun theTwoAirCircuitsAreNeverCompletedAcrossDifferentEcusOrBoots() {
        fun air(mid: Int,pid: Int,boot: Long=1)=reading(10.1,source="J1587/MID:$mid/PID:$pid").copy(unit="bar",bootId=boot)
        val old=mapOf(Instrument.AIR to InstrumentValue(air(140,117),air(140,118)))
        for(next in listOf(air(128,118),air(140,118,2))) {
            val result=retainCoreReadings(old,mapOf(Instrument.AIR to InstrumentValue(Reading(unit="bar",source=Origin.CORE),next)))[Instrument.AIR]!!
            assertNull(result.first.value)
            assertEquals(next,result.second)
        }
        val same=retainCoreReadings(old,mapOf(Instrument.AIR to InstrumentValue(air(140,117).copy(value=10.4))))[Instrument.AIR]!!
        assertEquals(10.4,same.first.value!!,0.0)
        assertEquals(10.1,same.second!!.value!!,0.0)
        assertEquals(Quality.STALE,same.second!!.quality)
    }
}
