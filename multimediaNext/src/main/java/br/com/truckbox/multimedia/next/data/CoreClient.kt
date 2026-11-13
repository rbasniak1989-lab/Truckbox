package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.*
import com.google.gson.Gson
import java.net.HttpURLConnection
import java.net.URL
import java.time.Instant

// Campos exatos observados nos clientes do repositório. Não buscar recursivamente em histórico/raw.
data class CoreStateDto(
    val oilPressureBar: Double? = null, val boostBar: Double? = null, val fuelPressureBar: Double? = null,
    val coolantTempC: Double? = null, val oilTempC: Double? = null, val intakeAirTempC: Double? = null,
    val gearboxTempC: Double? = null, val currentGear: Double? = null, val ambientTempC: Double? = null,
    val acceleratorPct: Double? = null, val actualTorquePct: Double? = null,
    val speedKph: Double? = null, val fuelRateLph: Double? = null,
)
data class CoreResult(val values: Map<Instrument,InstrumentValue>, val speed: Reading?, val flow: Reading?, val receivedAt: Instant)

fun CoreStateDto.toDomain(received: Instant): CoreResult {
    fun reading(value: Double?, unit: String) = Reading(
        value = value?.takeIf { it.isFinite() }, unit = unit, source = Origin.CORE,
        receivedAt = received, quality = if (value?.isFinite() == true) Quality.UNVERIFIED else Quality.MISSING,
    ) // HTTP não estabelece sampledAt, ECU ou qualidade: nenhuma leitura vira live.
    val values = mapOf(
        Instrument.OIL_PRESSURE to oilPressureBar, Instrument.BOOST to boostBar,
        Instrument.FUEL_PRESSURE to fuelPressureBar, Instrument.COOLANT to coolantTempC,
        Instrument.OIL_TEMP to oilTempC, Instrument.INTAKE to intakeAirTempC,
        Instrument.GEARBOX to gearboxTempC, Instrument.GEAR to currentGear,
        Instrument.AMBIENT to ambientTempC, Instrument.PEDAL to acceleratorPct, Instrument.TORQUE to actualTorquePct,
    ).mapValues { (key,value) -> InstrumentValue(reading(value,key.unit)) }
    return CoreResult(values,reading(speedKph,"km/h"),reading(fuelRateLph,"L/h"),received)
}

class CoreClient(private val gson: Gson = Gson()) {
    fun fetch(host: String): CoreResult? {
        // Os únicos hosts HTTP permitidos são locais; nenhuma credencial é enviada ao Core.
        require(host in setOf("truckbox.local","192.168.4.1"))
        val connection = URL("http://$host/api/state").openConnection() as HttpURLConnection
        return try {
            connection.connectTimeout = 1500; connection.readTimeout = 2000
            connection.requestMethod = "GET"; connection.instanceFollowRedirects = false
            connection.useCaches = false; connection.setRequestProperty("Accept","application/json")
            if (connection.responseCode !in 200..299) return null
            val body = connection.inputStream.use { input ->
                val output = java.io.ByteArrayOutputStream()
                val buffer = ByteArray(8192)
                while (output.size() <= 1_048_576) {
                    val count = input.read(buffer)
                    if(count < 0) break
                    output.write(buffer,0,count)
                }
                output.toByteArray()
            }
            if (body.isEmpty() || body.size > 1_048_576) return null
            gson.fromJson(body.toString(Charsets.UTF_8),CoreStateDto::class.java)?.toDomain(Instant.now())
        } finally { connection.disconnect() }
    }
}
