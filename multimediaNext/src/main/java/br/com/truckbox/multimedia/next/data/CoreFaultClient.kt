package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.EcuFault
import br.com.truckbox.multimedia.next.domain.EcuFaultSnapshot
import br.com.truckbox.multimedia.next.domain.parseFaultEntries
import com.google.gson.JsonParser
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.time.Instant

/**
 * Diagnóstico somente leitura do Core: GET /api/diagnostics paginado.
 * Falha da rede não é confirmação de inatividade nem autoriza limpar o histórico.
 */
class CoreFaultClient(
    private val connections: CoreConnectionFactory,
    private val now: () -> Instant = Instant::now,
) {
    fun fetch(host: String,expectedUid: String): EcuFaultSnapshot? {
        if(!validCoreHost(host) || expectedUid.isBlank()) return null
        for(candidate in linkedSetOf(host,"192.168.4.1","truckbox.local")) {
            val snapshot=runCatching {scan(candidate,expectedUid)}.getOrNull()
            if(snapshot!=null)return snapshot
        }
        return null
    }

    private fun scan(host:String,expectedUid:String):EcuFaultSnapshot? {
        var offset=0
        var total=32
        var pages=0
        var overflow=0
        var incomplete=false
        val faults=linkedMapOf<String,EcuFault>()
        do {
            if(pages++>=5)return null
            val url=URL("http://${host}/api/diagnostics?offset=${offset}&limit=8")
            var c:HttpURLConnection?=null
            val page=try {
                c=connections.open(url)
                c.requestMethod="GET"
                c.connectTimeout=1200;c.readTimeout=1600;c.instanceFollowRedirects=false;c.useCaches=false
                c.setRequestProperty("Accept","application/json")
                c.setRequestProperty("Cache-Control","no-cache")
                if(c.responseCode!=200)return null
                val data=ByteArrayOutputStream()
                c.inputStream.use {stream->
                    val buffer=ByteArray(2048)
                    while(true) {
                        val n=stream.read(buffer)
                        if(n<0)break
                        if(data.size()+n>32768)return null
                        data.write(buffer,0,n)
                    }
                }
                JsonParser.parseString(data.toString(Charsets.UTF_8.name())).asJsonObject
            } finally {c?.disconnect()}
            fun int(key:String)=runCatching {page.get(key)?.asInt}.getOrNull()
            fun string(key:String)=runCatching {page.get(key)?.asString}.getOrNull()
            val uid=string("device_uid") ?: return null
            if(uid!=expectedUid || runCatching {page.get("ok")?.asBoolean}.getOrNull()!=true) return null
            if(int("offset")!=offset || !page.has("faults") || !page.get("faults").isJsonArray)return null
            total=int("capacity")?.takeIf {it in 1..32} ?: return null
            val next=int("next_offset")?.takeIf {it>offset && it<=total} ?: return null
            overflow=maxOf(overflow,int("overflow")?:0)
            if(runCatching {page.get("persistent_ready")?.asBoolean}.getOrDefault(false)!=true)incomplete=true
            for(fault in parseFaultEntries(page))faults[fault.key]=fault
            offset=next
        } while(offset<total)
        return EcuFaultSnapshot(expectedUid,now(),faults.values.toList(),incomplete || overflow>0,overflow)
    }
}
