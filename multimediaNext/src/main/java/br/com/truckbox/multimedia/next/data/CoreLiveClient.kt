package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.*
import com.google.gson.JsonParser
import com.google.gson.JsonObject
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.time.Instant

/** Complemento de baixo custo para os três sinais publicados pelo /api/live da Core 0.2.26.
 * Sem substituir leitura paginada ou promover valor derivado a sinal validado.
 */
class CoreLiveClient(
    private val connections: CoreConnectionFactory,
    private val now: ()->Instant = Instant::now,
) {
    fun fetch(host:String,expectedUid:String):Map<Instrument,InstrumentValue> {
        if(!validCoreHost(host) || expectedUid.isBlank())return emptyMap()
        for(candidate in linkedSetOf(host,"192.168.4.1","truckbox.local")) {
            val result=runCatching {read(candidate,expectedUid)}.getOrNull()
            if(result!=null)return result
        }
        return emptyMap()
    }
    private fun read(host:String,uid:String):Map<Instrument,InstrumentValue>? {
        var connection:HttpURLConnection?=null
        return try {
            connection=connections.open(URL("http://${host}/api/live"))
            connection.connectTimeout=1200;connection.readTimeout=1800
            connection.requestMethod="GET";connection.instanceFollowRedirects=false
            connection.useCaches=false
            if(connection.responseCode!=200)return null
            val conn=connection
            val data=ByteArrayOutputStream().also {out->
                conn.inputStream.use {input->
                    val buffer=ByteArray(1024)
                    while(true) {
                        val n=input.read(buffer)
                        if(n<0)break
                        if(out.size()+n>4096)return null
                        out.write(buffer,0,n)
                    }
                }
            }.toByteArray()
            if(data.isEmpty() || data.size>4096)return null
            val json=JsonParser.parseString(data.toString(Charsets.UTF_8)).asJsonObject
            fun string(obj:JsonObject,key:String)=runCatching {obj.get(key)?.asString}.getOrNull()
            fun num(obj:JsonObject,key:String)=runCatching {obj.get(key)?.takeUnless {it.isJsonNull}?.asDouble }.getOrNull()?.takeIf {it.isFinite()}
            if(string(json,"device_uid")!=uid || string(json,"live_contract")!="TBX_CORE_LIVE_V1")return null
            val boot=num(json,"boot_id")?.toLong() ?: return null
            val meta=json.getAsJsonObject("live_readings") ?: return null
            val result=mutableMapOf<Instrument,InstrumentValue>()
            val at=now()
            fun insert(sensor:Instrument,key:String,valueKey:String,mid:Int,pid:Int,maxAge:Long,validQuality:String,reference:Boolean=false) {
                val info=runCatching {meta.getAsJsonObject(key)}.getOrNull() ?: return
                val age=num(info,"age_ms")?.toLong()?.takeIf {it in 0..maxAge} ?: return
                if(num(info,"mid")?.toInt()!=mid || num(info,"pid")?.toInt()!=pid)return
                if(string(info,"quality")!=validQuality)return
                if(reference && num(info,"reference_nm")!=2643.0)return
                val value=num(json,valueKey) ?: return
                if(sensor==Instrument.GEAR && (value%1.0!=0.0 || value !in -99.0..99.0))return
                val reading=Reading(value=value,unit=sensor.unit,source=Origin.CORE,
                    sampledAt=at.minusMillis(age),receivedAt=at,quality=Quality.PROVISIONAL,
                    sourceId="J1587/MID:${mid}/PID:${pid}",bootId=boot,
                    validation=validQuality)
                result[sensor]=InstrumentValue(reading)
            }
            insert(Instrument.GEAR,"gear","gear",130,163,5000L,"EM_VALIDACAO")
            insert(Instrument.TORQUE,"torque","actualTorquePct",128,93,65000L,"DERIVADO_REFERENCIA_HISTORICA_EM_VALIDACAO",true)
            insert(Instrument.FUEL_PRESSURE,"fuel_pressure","fuel_pressure_bar",128,94,15000L,"EM_VALIDACAO")
            result
        } finally {connection?.disconnect()}
    }
}
