package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.*
import com.google.gson.Gson
import com.google.gson.JsonParseException
import com.google.gson.annotations.SerializedName
import java.io.IOException
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.URL
import java.net.UnknownHostException
import java.time.Instant

/** Contrato real de telemetry.c em bd723891 (Core 0.2.23), não o catálogo de sensores. */
data class CoreSensorPageDto(
    val ok: Boolean? = null,
    @SerializedName("schema_version") val schemaVersion: Int? = null,
    val offset: Int? = null,
    val count: Int? = null,
    @SerializedName("observed_count") val observedCount: Int? = null,
    @SerializedName("next_offset") val nextOffset: Int? = null,
    val readings: List<CoreSensorDto>? = null,
)
data class CoreSensorDto(
    val source: String? = null, val sid: Int? = null, val pid: Int? = null,
    val sa: Int? = null, val mid: Int? = null, val pgn: Int? = null,
    @SerializedName("component_index") val componentIndex: Int? = null,
    @SerializedName("raw_only") val rawOnly: Boolean? = null,
    val unit: String? = null, val validation: String? = null, val quality: String? = null,
    @SerializedName("capture_id") val captureId: Long? = null,
    @SerializedName("boot_id") val bootId: Long? = null,
    @SerializedName("uptime_us") val uptimeUs: Long? = null,
    @SerializedName("age_ms") val ageMs: Long? = null,
    val stale: Boolean? = null, val value: Double? = null,
)
private data class SensorSpec(val key: String, val unit: String, val pgn: Int? = null, val priority: Int = 0)
private val canSpecs=mapOf(
    34 to SensorSpec("OIL_PRESSURE","bar",65263),29 to SensorSpec("BOOST","bar",65270),33 to SensorSpec("FUEL_PRESSURE","bar",65263),
    24 to SensorSpec("COOLANT","degC",65262),25 to SensorSpec("OIL_TEMP","degC",65262),30 to SensorSpec("INTAKE","degC",65270),
    27 to SensorSpec("GEARBOX","degC",65272),45 to SensorSpec("GEAR","gear",61445),28 to SensorSpec("AMBIENT","degC",65269),
    6 to SensorSpec("PEDAL","%",61443),2 to SensorSpec("TORQUE","%",61444),14 to SensorSpec("SPEED","km/h",65265),
)
private val j1587Specs=mapOf(
    100 to SensorSpec("OIL_PRESSURE","bar"),102 to SensorSpec("BOOST","bar (gauge)"),94 to SensorSpec("FUEL_PRESSURE","bar"),
    110 to SensorSpec("COOLANT","C"),175 to SensorSpec("OIL_TEMP","C"),105 to SensorSpec("INTAKE","C"),177 to SensorSpec("GEARBOX","C"),
    171 to SensorSpec("AMBIENT","C"),91 to SensorSpec("PEDAL","%"),158 to SensorSpec("VOLTAGE","V"),168 to SensorSpec("VOLTAGE","V",priority=1),
    117 to SensorSpec("AIR_1","bar"),118 to SensorSpec("AIR_2","bar"),84 to SensorSpec("SPEED","km/h"),183 to SensorSpec("FLOW","L/h"),
    // PID92 é carga, não torque. PID163 é faixa, não marcha. Não preencher esses instrumentos com proxies.
)

/** GET paginado, até 192 observações/24 páginas; nunca busca fila, catálogo, ACK ou configurações. */
class CoreSensorClient(
    private val connections: CoreConnectionFactory,
    private val gson: Gson = Gson(),
    private val now: () -> Instant = Instant::now,
    private val monotonic: () -> Long = System::nanoTime,
) {
    private data class Seen(val capture: Long, val uptime: Long, val value: Double?, val sampledAt: Instant)
    private val seen=mutableMapOf<String,Seen>()
    private var lastHost: String?=null

    fun fetch(preferredHost: String): CoreFetchResult {
        require(validCoreHost(preferredHost))
        if(lastHost!=preferredHost) {seen.clear();lastHost=preferredHost}
        val attempts=mutableListOf<CoreAttempt>()
        for(host in linkedSetOf(preferredHost,"192.168.4.1","truckbox.local")) {
            var status: Int?=null
            try {
                val candidates=mutableMapOf<String,MutableList<Pair<Int,Reading>>>()
                var offset=0
                var total: Int?=null
                var boot: Long?=null
                do {
                    var connection: HttpURLConnection?=null
                    val started=monotonic()
                    val page: CoreSensorPageDto
                    val received: Instant
                    try {
                        connection=connections.open(URL("http://$host/api/sensors?offset=$offset&limit=8&nocache=${now().toEpochMilli()}"))
                        connection.requestMethod="GET";connection.connectTimeout=1500;connection.readTimeout=2000
                        connection.useCaches=false;connection.instanceFollowRedirects=false
                        connection.setRequestProperty("Accept","application/json")
                        connection.setRequestProperty("Cache-Control","no-cache, no-store")
                        connection.setRequestProperty("Connection","close")
                        status=connection.responseCode
                        if(status !in 200..299) throw SensorHttpError()
                        val body=connection.inputStream.use { input -> input.readBounded() }
                        page=gson.fromJson(body,CoreSensorPageDto::class.java) ?: throw SensorFormatError()
                        received=now()
                    } finally { connection?.disconnect() }
                    val rows=page.readings ?: throw SensorFormatError()
                    val count=page.count ?: throw SensorFormatError()
                    val observed=page.observedCount ?: throw SensorFormatError()
                    val next=page.nextOffset ?: throw SensorFormatError()
                    if(page.ok!=true || page.schemaVersion!=2 || page.offset!=offset || count !in 0..8 || rows.size!=count ||
                        observed !in 0..192 || next!=offset+count || next>observed || (count==0 && offset<observed)) throw SensorFormatError()
                    if(total==null) total=observed
                    val roundTripMs=((monotonic()-started)/1_000_000).coerceAtLeast(0)
                    for(row in rows) {
                        val rowBoot=row.bootId ?: throw SensorFormatError()
                        if(boot!=null && boot!=rowBoot) throw SensorFormatError() // Não misturar boots entre páginas.
                        boot=rowBoot
                        map(row,received,roundTripMs)?.let { (key,priority,reading) -> candidates.getOrPut(key){mutableListOf()}.add(priority to reading) }
                    }
                    offset=next
                } while(offset<total)
                fun select(key: String): Reading? {
                    val options=candidates[key] ?: return null
                    val ranked=options.groupBy { (priority,reading) ->
                        (if(reading.observed(now())!=null) 1000 else 0)+(if(reading.quality==Quality.VALID) 100 else 0)+priority
                    }.maxByOrNull {it.key}?.value ?: return null
                    val values=ranked.mapNotNull {it.second.value}.distinct()
                    if(values.size>1 && ranked.map {it.second.sourceId}.distinct().size>1) return null // Origens em conflito.
                    return ranked.maxByOrNull {it.second.sampledAt ?: Instant.MIN}?.second
                }
                val values=Instrument.defaults.mapNotNull { instrument ->
                    if(instrument==Instrument.AIR) {
                        val first=select("AIR_1");val second=select("AIR_2")
                        // O par de circuitos pertence ao mesmo MID; nenhuma fusão entre ECUs.
                        if(first==null && second==null) null
                        else if(first!=null && second!=null && first.sourceId?.substringBefore("/PID")!=second.sourceId?.substringBefore("/PID")) null
                        else instrument to InstrumentValue(first ?: Reading(unit="bar",source=Origin.CORE),second)
                    } else select(instrument.name)?.let {instrument to InstrumentValue(it)}
                }.toMap()
                val speed=select("SPEED");val flow=select("FLOW")
                val chosen=values.values.flatMap {listOfNotNull(it.first,it.second)}+listOfNotNull(speed,flow)
                attempts+=CoreAttempt(host,httpStatus=status,resource="/api/sensors")
                if(seen.size>384) seen.clear() // Cache técnico volátil, limitado, sem payload bruto.
                return CoreFetchResult(CoreResult(values,speed,flow,now()),CoreDiagnostics(now(),attempts.toList(),chosen.count {it.value!=null},
                    sensorApi=true,validValues=chosen.count {it.current(now())!=null},provisionalValues=chosen.count {it.quality==Quality.PROVISIONAL && it.observed(now())!=null}))
            } catch (_: CoreWifiUnavailable) { attempts+=CoreAttempt(host,CoreIssue.NO_WIFI,resource="/api/sensors");break
            } catch (_: UnknownHostException) { attempts+=CoreAttempt(host,CoreIssue.DNS,status,"/api/sensors")
            } catch (_: SocketTimeoutException) { attempts+=CoreAttempt(host,CoreIssue.TIMEOUT,status,"/api/sensors")
            } catch (_: SensorHttpError) { attempts+=CoreAttempt(host,CoreIssue.HTTP,status,"/api/sensors")
            } catch (_: SensorFormatError) { attempts+=CoreAttempt(host,CoreIssue.FORMAT,status,"/api/sensors")
            } catch (_: JsonParseException) { attempts+=CoreAttempt(host,CoreIssue.FORMAT,status,"/api/sensors")
            } catch (_: IOException) { attempts+=CoreAttempt(host,CoreIssue.IO,status,"/api/sensors") }
        }
        // Compatibilidade explícita: firmware sem esse recurso ainda pode responder ao diagnóstico legado.
        if(attempts.any {it.httpStatus==404} && attempts.none {it.issue==CoreIssue.NO_WIFI}) {
            val legacy=CoreClient(connections,gson,now).fetch(preferredHost)
            return legacy.copy(diagnostics=legacy.diagnostics.copy(attempts=attempts+legacy.diagnostics.attempts))
        }
        return CoreFetchResult(null,CoreDiagnostics(now(),attempts.toList(),sensorApi=true))
    }

    private fun map(row: CoreSensorDto,received: Instant,roundTripMs: Long): Triple<String,Int,Reading>? {
        val spec=when(row.source) {
            "j1939" -> canSpecs[row.sid]?.takeIf {it.pgn==row.pgn && row.sa in 0..253}
            "j1587" -> j1587Specs[row.pid]?.takeIf {row.mid in 128..255}
            else -> null
        } ?: return null
        if(row.rawOnly==true || row.componentIndex!=null || row.unit!=spec.unit || row.captureId==null || row.captureId<1 ||
            row.bootId==null || row.bootId !in 0..4_294_967_295L || row.uptimeUs==null || row.uptimeUs<0 || row.ageMs==null || row.ageMs !in 0..604_800_000 || row.stale==null) return null
        val source=if(row.source=="j1939") "J1939/SA:${row.sa}/PGN:${row.pgn}/SID:${row.sid}" else "J1587/MID:${row.mid}/PID:${row.pid}"
        val identity="${row.bootId}/$source"
        var sampled=received.minusMillis(row.ageMs+roundTripMs)
        seen[identity]?.takeIf {it.capture==row.captureId}?.let { previous ->
            if(previous.uptime!=row.uptimeUs || previous.value!=row.value) throw SensorFormatError()
            if(previous.sampledAt.isBefore(sampled)) sampled=previous.sampledAt // Replay não rejuvenesce a captura.
        }
        val quality=when {
            row.value?.isFinite()!=true -> Quality.MISSING
            row.stale || row.ageMs+roundTripMs>5000 -> Quality.STALE
            row.quality=="VALIDO" && row.validation=="VALIDADO_NOS_LOGS" -> Quality.VALID
            row.quality in setOf("VALIDO","EM_VALIDACAO") && row.validation in setOf("EM_VALIDACAO","VALIDADO_NOS_LOGS") -> Quality.PROVISIONAL
            else -> Quality.INVALID
        }
        seen[identity]=Seen(row.captureId,row.uptimeUs,row.value,sampled)
        val unit=when(spec.key){"SPEED"->"km/h";"FLOW"->"L/h";"AIR_1","AIR_2"->"bar";else->Instrument.valueOf(spec.key).unit}
        return Triple(spec.key,spec.priority,Reading(row.value,unit,Origin.CORE,sampled,received,quality,source,row.captureId,row.bootId,row.validation))
    }

    private fun java.io.InputStream.readBounded(): String {
        val out=java.io.ByteArrayOutputStream();val buffer=ByteArray(4096)
        while(out.size()<=65_536) { val n=read(buffer);if(n<0) break;out.write(buffer,0,n) }
        if(out.size()==0 || out.size()>65_536) throw SensorFormatError()
        return out.toString(Charsets.UTF_8.name())
    }
    private class SensorHttpError : IOException()
    private class SensorFormatError : IOException()
}
