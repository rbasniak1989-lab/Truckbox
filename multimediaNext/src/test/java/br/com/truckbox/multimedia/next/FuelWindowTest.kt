package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.data.CloudFuelDto
import br.com.truckbox.multimedia.next.data.CloudLegDto
import br.com.truckbox.multimedia.next.data.parseCoreFuel
import br.com.truckbox.multimedia.next.domain.FuelQuality
import org.junit.Assert.*
import org.junit.Test
import java.time.Instant

class FuelWindowTest {
    private val now = Instant.parse("2026-11-22T22:00:00Z")
    private val message = """{"contract":"TBX_FUEL_10S_V2","device_uid":"core","uptime_ms":11001,
        "w183":[[1,0,18889,10000,10,2000,50000,18889]]}"""

    @Test fun usesCoreWindowWithNoWallClockOrCloud() {
        val window = requireNotNull(parseCoreFuel(message, "core", now))
        assertEquals(6.8, window.averageLph!!, 0.0001)
        assertEquals(now.minusMillis(1001), window.endedAt)
        assertNull(parseCoreFuel(message, "another-core", now))
        assertNull(parseCoreFuel(message.replace("11001", "41001"), "core", now))
        assertNull(parseCoreFuel(message.replace(",10000,10,", ",5000,10,"), "core", now)?.averageLph)
    }

    @Test fun preservesAuthoritativeAverageAndQuality() {
        val leg = CloudLegDto("loaded:x", "LOADED", 742.0, 542.0, 742.0/542,
            CloudFuelDto(FuelQuality.RECONCILED_PID250, now.toString())).domain()
        assertEquals(742.0/542, leg.averageKml!!, 0.000001)
        assertEquals("Conferido pelo PID 250", leg.fuel?.label(now))
        assertEquals("Leitura desatualizada", leg.fuel?.label(now.plusSeconds(36)))
    }

    @Test fun localTripUsesOnlyCoreTotalsAndNeedsAnOdometerForTheAverage() {
        val withTrip=message.dropLast(1)+",\"local_trip\":{\"litres\":2,\"distance_km\":10,\"gaps\":true}}"
        val trip=requireNotNull(parseCoreFuel(withTrip,"core",now)?.trip)
        assertEquals(5.0,trip.averageKml!!,0.0)
        assertEquals("Estimativa local · lacunas",trip.label)
        assertNull(parseCoreFuel(withTrip.replace(",\"distance_km\":10",""),"core",now)?.trip?.averageKml)
    }
}
