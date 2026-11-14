package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.domain.*
import org.junit.Assert.*
import org.junit.Test
import java.time.Instant

class TankLedgerTest {
    private val t=Instant.parse("2026-11-13T12:00:00Z")
    private val full=TankEvent("full","v",t,TankEventKind.BOTH_FULL,1000.0,"boot1",bothTanksConfirmed=true)
    private fun counter(id: String,seconds: Long,n: Double)=TankEvent(id,"v",t.plusSeconds(seconds),TankEventKind.COUNTER,n,"boot1")
    @Test fun fullDebitPartialIdleDebitSequenceIs720570670650() {
        val a=listOf(full,counter("150",1,1150.0))
        assertEquals(720.0,reconcileTank(720.0,"v",listOf(full)).remainingLiters!!,0.0)
        assertEquals(570.0,reconcileTank(720.0,"v",a).remainingLiters!!,0.0)
        val b=a+TankEvent("partial","v",t.plusSeconds(2),TankEventKind.PARTIAL,addedLiters=100.0)
        assertEquals(670.0,reconcileTank(720.0,"v",b).remainingLiters!!,0.0)
        assertEquals(650.0,reconcileTank(720.0,"v",b+counter("20-including-idle",3,1170.0)).remainingLiters!!,0.0)
    }
    @Test fun replayOutOfOrderAndProcessReconstructionAreIdempotent() {
        val events=listOf(full,counter("a",1,1100.0),counter("b",2,1150.0))
        val expected=reconcileTank(720.0,"v",events)
        assertEquals(expected,reconcileTank(720.0,"v",events.reversed()+events))
        // Recria a partir de registros imutáveis como uma nova instância após reinício.
        assertEquals(expected,reconcileTank(720.0,"v",events.map { it.copy() }))
    }
    @Test fun oneTankFullAndMissingCalibrationNeverInitializeBalance() {
        assertNull(reconcileTank(720.0,"v",listOf(full.copy(bothTanksConfirmed=false))).remainingLiters)
        assertEquals(TankQuality.UNCALIBRATED,reconcileTank(720.0,"v",listOf(counter("a",1,1150.0))).quality)
    }
    @Test fun resetRolloverOrGapMakeEstimateStaleUntilNewCalibration() {
        val a=listOf(full,counter("a",1,1100.0))
        assertEquals(TankQuality.STALE,reconcileTank(720.0,"v",a+counter("reset",2,1.0)).quality)
        assertEquals(TankQuality.STALE,reconcileTank(720.0,"v",a+counter("reset",2,1200.0).copy(counterEpoch="boot2")).quality)
        assertEquals(TankQuality.STALE,reconcileTank(720.0,"v",a+TankEvent("gap","v",t.plusSeconds(2),TankEventKind.GAP)).quality)
        val recalibration=full.copy(id="full2",at=t.plusSeconds(3),counterLiters=1.0,counterEpoch="boot2")
        assertEquals(TankQuality.VALID,reconcileTank(720.0,"v",a+counter("reset",2,1.0)+recalibration).quality)
    }
    @Test fun conflictingDuplicateAndOverspendingAreNotClampedToHideError() {
        assertEquals(TankQuality.CONFLICT,reconcileTank(720.0,"v",listOf(full,full.copy(counterLiters=1001.0))).quality)
        assertEquals(TankQuality.CONFLICT,reconcileTank(720.0,"v",listOf(full,counter("bad",1,2000.0))).quality)
    }
    @Test fun partialCannotExceedCapacityOrResetAndOtherVehicleIsIsolated() {
        val partial=TankEvent("partial","v",t.plusSeconds(2),TankEventKind.PARTIAL,addedLiters=100.0)
        assertEquals(720.0,reconcileTank(720.0,"v",listOf(full,partial)).remainingLiters!!,0.0)
        assertEquals(720.0,reconcileTank(720.0,"v",listOf(full,counter("other",1,1700.0).copy(vehicleId="other"))).remainingLiters!!,0.0)
        // Não existe campo/PID de boia em TankEvent: oscilação de nível não entra na API do cálculo.
        assertFalse(TankEvent::class.java.declaredFields.any { it.name.contains("level",true) || it.name.contains("boia",true) })
    }
}
