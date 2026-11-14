package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.*
import com.google.gson.Gson
import com.google.gson.JsonParseException
import java.io.IOException
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.UnknownHostException
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
data class CoreResult(val values: Map<Instrument,InstrumentValue>, val speed: Reading?, val flow: Reading?, val receivedAt: Instant,val physicalDeviceUid: String?=null,val internetSharing: CoreInternetSharing?=null)
data class CoreFetchResult(val response: CoreResult?, val diagnostics: CoreDiagnostics)

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

class CoreClient(
    private val connections: CoreConnectionFactory,
    private val gson: Gson = Gson(),
    private val now: () -> Instant = Instant::now,
) {
    fun fetch(preferredHost: String): CoreFetchResult {
        require(validCoreHost(preferredHost))
        val attempts = mutableListOf<CoreAttempt>()
        for (host in linkedSetOf(preferredHost,"192.168.4.1","truckbox.local")) {
            var connection: HttpURLConnection? = null
            var status: Int? = null
            try {
                // Somente GET local; fallback de endereço não consome lotes nem envia ACK/reset/GPS.
                connection = connections.open(URL("http://$host/api/state?nocache=${now().toEpochMilli()}"))
                connection.connectTimeout = 1500; connection.readTimeout = 2000
                connection.requestMethod = "GET"; connection.instanceFollowRedirects = false
                connection.useCaches = false
                connection.setRequestProperty("Accept","application/json")
                connection.setRequestProperty("Cache-Control","no-cache, no-store")
                status = connection.responseCode
                if (status !in 200..299) {
                    attempts += CoreAttempt(host,CoreIssue.HTTP,status)
                    continue
                }
                val body = connection.inputStream.use { input ->
                    val output = java.io.ByteArrayOutputStream()
                    val buffer = ByteArray(8192)
                    while (output.size() <= MAX_BODY_BYTES) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        output.write(buffer,0,count)
                    }
                    output.toByteArray()
                }
                if (body.isEmpty() || body.size > MAX_BODY_BYTES) {
                    attempts += CoreAttempt(host,CoreIssue.FORMAT,status)
                    continue
                }
                val result = gson.fromJson(body.toString(Charsets.UTF_8),CoreStateDto::class.java)?.toDomain(now())
                if (result == null) {
                    attempts += CoreAttempt(host,CoreIssue.FORMAT,status)
                    continue
                }
                attempts += CoreAttempt(host,httpStatus=status)
                val count = result.values.values.sumOf { listOfNotNull(it.first.value,it.second?.value).size } +
                    listOfNotNull(result.speed?.value,result.flow?.value).size
                return CoreFetchResult(result,CoreDiagnostics(now(),attempts.toList(),count))
            } catch (_: CoreWifiUnavailable) {
                attempts += CoreAttempt(host,CoreIssue.NO_WIFI)
                break // Não tentar a rede móvel quando o Wi-Fi do Core estiver ausente.
            } catch (_: UnknownHostException) {
                attempts += CoreAttempt(host,CoreIssue.DNS,status)
            } catch (_: SocketTimeoutException) {
                attempts += CoreAttempt(host,CoreIssue.TIMEOUT,status)
            } catch (_: JsonParseException) {
                attempts += CoreAttempt(host,CoreIssue.FORMAT,status)
            } catch (_: IOException) {
                attempts += CoreAttempt(host,CoreIssue.IO,status)
            } finally {
                connection?.disconnect()
            }
        }
        return CoreFetchResult(null,CoreDiagnostics(now(),attempts.toList()))
    }

    private companion object { const val MAX_BODY_BYTES = 1_048_576 }
}
