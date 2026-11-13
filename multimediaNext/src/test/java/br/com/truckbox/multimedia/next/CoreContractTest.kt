package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.data.*
import br.com.truckbox.multimedia.next.domain.*
import com.google.gson.Gson
import org.junit.Assert.*
import org.junit.Test
import java.time.Instant

class CoreContractTest {
    @Test fun legacyResponseDoesNotProveSourceTimestampOrQuality() {
        val now=Instant.now()
        val dto=Gson().fromJson("""{"fuelPressureBar":4.5,"oilPressureBar":4.4,"speedKph":0,"history":[{"boostBar":99}]}""",CoreStateDto::class.java)
        val result=dto.toDomain(now)
        assertNull(result.values[Instrument.FUEL_PRESSURE]!!.first.current(now))
        assertEquals(Quality.UNVERIFIED,result.values[Instrument.FUEL_PRESSURE]!!.first.quality)
        assertNull(result.values[Instrument.BOOST]!!.first.value)
        assertNull(result.speed!!.current(now))
    }
}
