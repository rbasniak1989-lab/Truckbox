package br.com.truckbox.multimedia.next

import br.com.truckbox.multimedia.next.domain.*
import java.time.Instant

/** Compilado somente no APK .demo. Não há rede nem credenciais neste ambiente. */
private object PreviewData {
    fun state(): AppState {
        val now = Instant.now()
        val numbers = listOf(4.4,1.8,null,87.0,92.0,38.0,76.0,12.0,27.6,24.0,62.0,10.1,74.0)
        fun reading(value: Double?, unit: String) = Reading(value,unit,Origin.DEMO,now,now,if(value==null) Quality.MISSING else Quality.VALID,"DEMO")
        val readings = Instrument.defaults.mapIndexed { i,s ->
            s to InstrumentValue(reading(numbers[i],s.unit),if(s==Instrument.AIR) reading(10.4,s.unit) else null)
        }.toMap()
        val leg = LegConsumption("demo-leg",LegKind.LOADED,222.4,126.8)
        return AppState(
            demo = true,readings = readings,core = ConnectionState.DEMO,cloud = ConnectionState.DEMO,
            speed = reading(70.0,"km/h"),fuelRate = reading(42.0,"L/h"),
            trip = TripView("demo-trip",leg.legId,"Guarapuava/PR","Paranaguá/PR",LegKind.LOADED,leg,"Demonstração",48.5,150.0),
            legs = listOf(leg),last100Kml = 1.82,tankLiters = 650.0,tankStatus = "Estimativa SIMULADA",
            policyLabel = "DEMONSTRAÇÃO · sem autorização real",
            series = readings.filterKeys { it != Instrument.AIR && it != Instrument.GEAR }.mapValues { (_,v) ->
                (0..90).map { i -> TimeSample("demo-$i",now.minusSeconds(900-i*10L),v.first.value,if(v.first.value==null) Quality.MISSING else Quality.VALID,"DEMO") }
            },
        )
    }
}
internal fun initialState() = PreviewData.state()
