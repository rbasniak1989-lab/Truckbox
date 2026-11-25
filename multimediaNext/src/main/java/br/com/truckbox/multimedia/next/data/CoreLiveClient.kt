package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.*
import com.google.gson.JsonParser
import com.google.gson.JsonObject
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.time.Instant

const val LIVE_TORQUE_TTL_MS = 2500L
const val LIVE_GEAR_TTL_MS = 5000L

/** Leitura dos instrumentos do Core pela rede local. */
class CoreLiveClient(
    private val connections: CoreConnectionFactory,
    private val now: ()->Instant = Instant::now,
    private val monotonic: ()->Long = System::nanoTime,
) {
    private data class Seen(val boot: Long, val stamp: Long, val value: Double, val at: Instant)
    private val seen = mutableMapOf<Instrument, Seen>()
    private var coreUid: String? = null

    fun fetch(host:String,expectedUid:String):Map<Instrument,InstrumentValue> {
        if(!validCoreHost(host) || expectedUid.isBlank())return emptyMap()
        if (coreUid != expectedUid) {
            seen.clear()
            coreUid = expectedUid
        }
        for(candidate in linkedSetOf(host,"192.168.4.1","truckbox.local")) {
            val result=runCatching {read(candidate,expectedUid)}.getOrNull()
            if(result!=null)return result
        }
        return emptyMap()
    }
    private fun read(host:String,uid:String):Map<Instrument,InstrumentValue>? {
        val started = monotonic()
        var connection:HttpURLConnection?=null
        return try {
            connection=connections.open(URL("http://${host}/api/live"))
            connection.connectTimeout=1200;connection.readTimeout=1800
            connection.requestMethod="GET";connection.instanceFollowRedirects=false
            connection.useCaches=false
            connection.setRequestProperty("Cache-Control", "no-cache, no-store")
            if(connection.responseCode!=200)return null
            val conn=connection
            val data=ByteArrayOutputStream().also {out->
                conn.inputStream.use {input->
                    val buffer=ByteArray(1024)
                    while(true) {
                        val n=input.read(buffer)
                        if(n<0)break
                        if(out.size()+n>6144)return null
                        out.write(buffer,0,n)
                    }
                }
            }.toByteArray()
            if(data.isEmpty())return null
            val json=JsonParser.parseString(data.toString(Charsets.UTF_8)).asJsonObject
            fun string(obj:JsonObject,key:String)=runCatching {obj.get(key)?.asString}.getOrNull()
            fun num(obj:JsonObject,key:String)=runCatching {obj.get(key)?.takeUnless {it.isJsonNull}?.asDouble }.getOrNull()?.takeIf {it.isFinite()}
            if(string(json,"device_uid")!=uid || string(json,"live_contract")!="TBX_CORE_LIVE_V1")return null
            val boot=num(json,"boot_id")?.toLong() ?: return null
            val meta=json.getAsJsonObject("live_readings") ?: return null
            val stamps=json.getAsJsonObject("live_sample_v1")
            val result=mutableMapOf<Instrument,InstrumentValue>()
            val at=now()
            val transferMs = ((monotonic() - started) / 1_000_000).coerceAtLeast(0)
            fun insert(sensor:Instrument,key:String,valueKey:String,mid:Int,pid:Int,maxAge:Long,validQuality:String,unit:String=sensor.unit) {
                val info=runCatching {meta.getAsJsonObject(key)}.getOrNull() ?: return
                val age=num(info,"age_ms")?.toLong()?.takeIf {it in 0..maxAge} ?: return
                if (age + transferMs > maxAge) return
                if(num(info,"mid")?.toInt()!=mid || num(info,"pid")?.toInt()!=pid)return
                if(string(info,"quality")!=validQuality)return
                val value=num(json,valueKey) ?: return
                val label = if (sensor == Instrument.GEAR) {
                    val rawLabel = string(json, "gear_text")
                    if (value % 1.0 != 0.0 || value !in -99.0..99.0 || (value == 0.0 && rawLabel != "N")) return
                    rawLabel?.takeIf { (value == 0.0 && it == "N") ||
                        (value < 0 && (it == "R" || it == "R${-value.toInt()}")) }
                } else null
                val stamp = stamps?.let { num(it, "${key}_uptime_ms") }?.toLong()?.takeIf { it > 0 }
                var sampledAt = at.minusMillis(age + transferMs)
                if (stamp != null) {
                    seen[sensor]?.takeIf { it.boot == boot }?.let { previous ->
                        if (stamp < previous.stamp || (stamp == previous.stamp && value != previous.value)) return
                        if (stamp == previous.stamp) sampledAt = minOf(sampledAt, previous.at)
                    }
                    seen[sensor] = Seen(boot, stamp, value, sampledAt)
                    if (sampledAt.plusMillis(maxAge).isBefore(at)) return
                }
                val reading=Reading(value=value,unit=unit,source=Origin.CORE,
                    sampledAt=sampledAt,receivedAt=at,quality=Quality.PROVISIONAL,
                    sourceId="J1587/MID:${mid}/PID:${pid}",bootId=boot,
                    validation=validQuality,sampleUptimeMs=stamp,displayLabel=label)
                result[sensor]=InstrumentValue(reading)
            }
            insert(Instrument.GEAR,"gear","gear",130,163,LIVE_GEAR_TTL_MS,"EM_VALIDACAO")
            val torque = meta.getAsJsonObject("torque")
            if (torque != null && num(torque,"reference_nm") == 2643.0) {
                insert(Instrument.TORQUE,"torque","actualTorquePct",128,93,LIVE_TORQUE_TTL_MS,
                    "DERIVADO_REFERENCIA_HISTORICA_EM_VALIDACAO")
            } else {
                insert(Instrument.TORQUE,"torque","torqueNm",128,93,LIVE_TORQUE_TTL_MS,"EM_VALIDACAO","N·m")
            }
            insert(Instrument.FUEL_PRESSURE,"fuel_pressure","fuel_pressure_bar",128,94,15000L,"EM_VALIDACAO")
            result
        } finally {connection?.disconnect()}
    }
}
