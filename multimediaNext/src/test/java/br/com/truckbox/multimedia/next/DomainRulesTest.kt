package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.domain.*
import org.junit.Assert.*
import org.junit.Test
import java.time.Instant

class DomainRulesTest {
    private val now=Instant.parse("2026-11-13T12:00:00Z")
    private fun reading(v: Double,at: Instant=now,quality: Quality=Quality.VALID)=Reading(v,"",Origin.CORE,at,now,quality,"ECU")

    @Test fun homeHasExactlyThirteenInApprovedOrderAndDualAirIsOne() {
        assertEquals(listOf("OIL_PRESSURE","BOOST","FUEL_PRESSURE","COOLANT","OIL_TEMP","INTAKE","GEARBOX","GEAR","VOLTAGE","AMBIENT","PEDAL","AIR","TORQUE"),Instrument.defaults.map { it.name })
        assertEquals(5,Page.entries.size)
        assertEquals(listOf("GENERAL","ENGINE","INTAKE","TRANSMISSION","BRAKES","ELECTRICAL"),TruckSystem.entries.map { it.name })
    }
    @Test fun staleUnknownFutureAndNonFiniteNeverBecomeCurrent() {
        assertNull(reading(4.0,now.minusSeconds(6)).current(now))
        assertNull(reading(4.0,now.plusSeconds(1)).current(now))
        assertNull(reading(4.0,quality=Quality.UNVERIFIED).current(now))
        assertNull(reading(Double.NaN).current(now))
        assertNull(reading(4.0).copy(sourceId=null).current(now))
    }
    @Test fun instantaneousConsumptionUsesLphWhenStoppedAndIncludesIdle() {
        assertEquals(InstantConsumption(3.0,"L/h"),instantConsumption(reading(0.0),reading(3.0),now))
        assertEquals(InstantConsumption(2.0,"km/l"),instantConsumption(reading(80.0),reading(40.0),now))
        assertNull(instantConsumption(reading(80.0),reading(0.0),now))
        assertNull(instantConsumption(null,reading(3.0),now))
    }
    @Test fun loadedAndEmptyStaySeparate() {
        val loaded=LegConsumption("a",LegKind.LOADED,100.0,60.0)
        val empty=LegConsumption("b",LegKind.EMPTY,100.0,30.0)
        assertEquals(100.0/60,loaded.averageKml!!,0.0001)
        assertEquals(100.0/30,empty.averageKml!!,0.0001)
    }
    @Test fun averageIsWeightedByTimeAndDeduplicatesFrames() {
        val samples=listOf(TimeSample("a",now,10.0,Quality.VALID,"ECU"),TimeSample("b",now.plusSeconds(1),20.0,Quality.VALID,"ECU"),TimeSample("c",now.plusSeconds(10),30.0,Quality.VALID,"ECU"))
        val stats=timeStats(samples+samples.first(),now,now.plusSeconds(10))!!
        assertEquals(19.0,stats.mean,.0001)
        assertEquals(10.0,stats.min,0.0);assertEquals(30.0,stats.max,0.0)
        assertEquals(1.0,stats.coverage,0.0)
    }
    @Test fun gapsInvalidSamplesAndConflictingReplaysPreventFalseStatistics() {
        val a=TimeSample("a",now,10.0,Quality.VALID,"ECU")
        val b=TimeSample("b",now.plusSeconds(100),20.0,Quality.VALID,"ECU")
        assertNull(timeStats(listOf(a,b),now,now.plusSeconds(100)))
        assertNull(timeStats(listOf(a,a.copy(value=20.0)),now,now.plusSeconds(10)))
        assertNull(timeStats(listOf(a,b.copy(at=now.plusSeconds(2),quality=Quality.INVALID)),now,now.plusSeconds(10)))
    }
    @Test fun faultLossDoesNotCreateInactiveTimestampAndRecurrenceKeepsEpisode() {
        val active=FaultEpisode("episode-1","SPN supplied by ECU","J1939",now,now)
        val unknown=active.lostCommunication()
        assertEquals(FaultState.UNKNOWN,unknown.state);assertNull(unknown.deactivatedAt)
        assertEquals(now,unknown.lastSeenActive)
        val inactive=unknown.confirmedInactive(now.plusSeconds(10))
        val next=FaultEpisode("episode-2",active.code,active.source,now.plusSeconds(20),now.plusSeconds(20))
        assertEquals(now.plusSeconds(10),inactive.deactivatedAt)
        assertNotEquals(inactive.id,next.id)
    }
    @Test fun journeyWindowTotalsExactly24hAndStoppedIsSubtotal() {
        val start=now.minusSeconds(86400)
        val s=journey24h(listOf(JourneySegment(start,start.plusSeconds(3600),JourneyKind.DRIVING),JourneySegment(start.plusSeconds(3600),start.plusSeconds(7200),JourneyKind.REST_CONFIRMED,true)),now)
        assertEquals(86400L,s.total);assertEquals(3600L,s.stopped);assertEquals(3600L,s.rest);assertEquals(79200L,s.unknown)
    }
    @Test fun stoppedOrUnconfirmedRestNeverBecomesLegalRest() {
        val s=journey24h(listOf(JourneySegment(now.minusSeconds(2400),now,JourneyKind.REST_CONFIRMED,false)),now)
        assertEquals(0L,s.rest);assertEquals(86400L,s.unknown)
        val conflict=journey24h(listOf(JourneySegment(now.minusSeconds(3600),now,JourneyKind.DRIVING),JourneySegment(now.minusSeconds(1800),now,JourneyKind.REST_CONFIRMED,true)),now)
        assertEquals(1800L,conflict.driving);assertEquals(0L,conflict.rest)
    }
    @Test fun tollForecastIsReplacedAndCarrierDoesNotReduceOwnerResult() {
        val predicted=TollEvent("pass-1",1,"loaded",TollStatus.FORECAST,TollPayer.OWNER,12000)
        val detected=predicted.copy(revision=2,status=TollStatus.IDENTIFIED,amountCents=13000)
        val carrier=TollEvent("pass-2",1,"loaded",TollStatus.CONFIRMED,TollPayer.CARRIER,15000)
        val empty=TollEvent("pass-3",1,"empty",TollStatus.IDENTIFIED,TollPayer.OWNER,10000)
        val result=tollProjection(listOf(predicted,detected,detected,carrier,empty),"loaded")
        assertEquals(13000L,result.ownCents);assertFalse(result.hasUnknownPrice)
        assertEquals(TollStatus.IDENTIFIED,detected.status)
    }
    @Test fun nightUsesSaoPauloBoundariesAndManualOverride() {
        assertFalse(isNight(Instant.parse("2026-11-13T20:59:00Z"),Appearance.AUTO))
        assertTrue(isNight(Instant.parse("2026-11-13T21:00:00Z"),Appearance.AUTO))
        assertTrue(isNight(Instant.parse("2026-11-13T08:59:00Z"),Appearance.AUTO))
        assertFalse(isNight(Instant.parse("2026-11-13T09:00:00Z"),Appearance.AUTO))
        assertTrue(isNight(now,Appearance.NIGHT));assertFalse(isNight(now,Appearance.DAY))
    }
    @Test fun contradictoryTollRevisionsAreMarkedForReviewRegardlessOfReplayOrder() {
        val a=TollEvent("passage",1,"leg",TollStatus.IDENTIFIED,TollPayer.OWNER,10000)
        val b=a.copy(amountCents=20000)
        val result=tollProjection(listOf(a,b),"leg")
        assertTrue(result.hasUnknownPrice)
        assertEquals(result,tollProjection(listOf(b,a),"leg"))
    }
    @Test fun dashboardEditingRequiresVerifiedStopAndAuthorization() {
        assertFalse(canEditDashboard(null,now,true))
        assertFalse(canEditDashboard(reading(0.0),now,false))
        assertFalse(canEditDashboard(reading(1.0),now,true))
        assertFalse(canEditDashboard(reading(0.0,now.minusSeconds(9)),now,true))
        assertTrue(canEditDashboard(reading(0.0),now,true))
    }
}
