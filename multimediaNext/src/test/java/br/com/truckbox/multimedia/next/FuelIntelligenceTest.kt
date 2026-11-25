package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.data.*
import br.com.truckbox.multimedia.next.domain.*
import com.google.gson.Gson
import org.junit.Assert.*
import org.junit.Test
import java.time.Instant

class FuelIntelligenceTest {
    private val now = Instant.parse("2026-11-26T15:00:00Z")
    private fun signal(value: Double, quality: Quality = Quality.VALID, at: Instant = now) =
        Reading(value, "", Origin.CORE, at, at, quality, "J1587/PID")

    @Test fun consumesTheCloudBalanceAndConventionalRangeDirectly() {
        val body = """{"fuelRange":{"capacityLiters":720,"remainingLiters":610.2,"percent":84.75,
            "fullTankAt":"2026-11-25T10:00:00+00:00","consumedSinceFullLiters":109.8,
            "partialAddedLiters":0,"averageKml":1.0,"averageSource":"ACTIVE_TRIP",
            "autonomyKm":608.6,"quality":"APPROXIMATE"}}"""
        val dto = Gson().fromJson(body, CloudSnapshotDto::class.java)
        val fuel = requireNotNull(dto.fuelRange).domain(now)
        assertEquals(720.0, fuel.capacityLiters)
        assertEquals(610.2, fuel.remainingLiters)
        assertEquals(608.6, fuel.autonomyKm)
        assertEquals("Média da viagem", fuel.averageLabel)
        assertEquals("Aproximada", fuel.qualityLabel)
        assertEquals(now, fuel.updatedAt)
        assertNull(FuelRangeDto(capacityLiters = 720.0, remainingLiters = 721.0, autonomyKm = 5.0).domain(now).autonomyKm)
    }

    @Test fun showsLocalMotionStopAndConfirmedCutWithoutCloud() {
        assertEquals("L/h", displayedConsumption(signal(0.0), signal(3.0), null, now)?.unit)
        val moving = displayedConsumption(signal(80.0), signal(40.0, Quality.PROVISIONAL), null, now)!!
        assertEquals(2.0, moving.value)
        assertEquals("km/L", moving.unit)
        assertTrue(moving.explanation.contains("provisório"))
        val cut = displayedConsumption(signal(80.0), signal(0.0), null, now)!!
        assertTrue(cut.explanation.contains("Corte"))
        assertNull(cut.value)
        assertNull(displayedConsumption(signal(80.0), signal(0.0, Quality.PROVISIONAL), null, now))
        assertNull(displayedConsumption(signal(80.0), signal(40.0, at = now.minusSeconds(8)), null, now))
    }

    @Test fun usesOnlyACompleteFreshIntegratedTenSecondWindowAsFallback() {
        val window = LocalFuelWindow("core", now.minusSeconds(12), 10_000, 100_000)
        val fallback = displayedConsumption(signal(70.0), null, window, now)!!
        assertEquals(36.0, fallback.value)
        assertTrue(fallback.explanation.contains("Média ponderada"))
        assertNull(displayedConsumption(null, null, window.copy(coveredMs = 5_000), now))
        assertNull(displayedConsumption(null, null, window.copy(endedAt = now.minusSeconds(31)), now))
    }

    @Test fun localHistoryUsesMeasuredDeltasSkipsGapsAndClearsAfterReset() {
        val meter = LocalFuelHistory()
        fun observation(time: Long, km: Double, liters: Double, covered: Long, boot: Long = 1) =
            LocalFuelWindow("core", now.plusMillis(time), 10_000, 0,
                LocalTripFuel(km, liters, false, covered), boot, time)
        assertNull(meter.observe(observation(10_000, 49.0, 20.0, 10_000)))
        val first = meter.observe(observation(20_000, 49.2, 20.2, 20_000))!!
        assertEquals(.2, first.rows.single().liters, .00001)
        assertEquals(1, first.between(0.0, 50.0).size)
        assertTrue(first.between(50.0, 100.0).isEmpty())
        val missed = meter.observe(observation(70_000, 50.1, 20.5, 30_000))!!
        assertEquals(1, missed.rows.size)
        val later = meter.observe(observation(80_000, 50.3, 20.8, 40_000))!!
        assertEquals(1, later.between(50.0, 100.0).size)
        assertTrue(meter.observe(observation(2_000, 0.0, 0.0, 1_000, boot = 2))!!.rows.isEmpty())
    }

    @Test fun closedFuelWindowUsesOnlyTheCoveredSpeedIntervalAndIgnoresDuplicateSlots() {
        val meter = WindowFuelHistory()
        for (second in 0..12) {
            val at = now.plusSeconds(second.toLong())
            val speed = Reading(72.0, "km/h", Origin.CORE, at, at, Quality.VALID, "J1587/MID:128/PID:84",
                captureId = second + 1L, bootId = 42)
            meter.speedReading("core", speed, at)
        }
        val sample = LocalFuelWindow("core", now.plusSeconds(10), 10_000, 100_000, bootId = 42, slot = 1)
        val history = meter.observe(sample)!!
        assertEquals(.2, history.rows.single().coveredKm, .00001)
        assertEquals(.1, history.rows.single().liters, .00001)
        assertNull(meter.observe(sample))
        assertNull(meter.observe(sample.copy(endedAt = now.plusSeconds(20), slot = 2)))
    }

    @Test fun forecastMustMatchTheTerrainMethodAndTheCurrentRevision() {
        val route = JourneyRoute("route", "trip", 3, 100_000.0, emptyList(), emptyList(), now.plusSeconds(1_000), 18_000.0)
        val horizon = FuelHorizonDto(distanceM = 18_000.0, liters = 9.0, averageKml = 2.0, terrain = "FLAT")
        val dto = RouteFuelForecastDto("TRUCK_TERRAIN_HISTORY_V1", "trip", "route", 3, "test-v1", "MEDIUM", 500, now.toString(),
            FuelHorizonsDto(horizon, horizon, horizon))
        assertEquals(9.0, dto.domain(route, now).destination?.liters)
        assertEquals(18_000.0, dto.domain(route, now).next100?.distanceM)
        assertThrows(IllegalArgumentException::class.java) { dto.copy(method = "ACTIVE_TRIP_AVERAGE").domain(route, now) }
        assertThrows(IllegalArgumentException::class.java) { dto.copy(routeRevision = 2).domain(route, now) }
        val state = AppState(route = route, routeForecast = dto.domain(route, now), operationalExpiresAt = now.plusSeconds(30))
        assertNotNull(state.redacted(now).routeForecast)
        assertNull(state.redacted(now.plusSeconds(31)).routeForecast)
    }
}
