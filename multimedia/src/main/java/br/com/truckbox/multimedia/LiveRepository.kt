package br.com.truckbox.multimedia

import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.CompletableFuture
import java.util.concurrent.Executor
import java.util.concurrent.ForkJoinPool

internal data class CloudTripSummary(
    val distance: Double?, val fuel: Double?, val average: Double?,
    val origin: String?, val destination: String?, val weight: Double?, val mode: String?,
)

internal fun cloudTripSummary(j: JSONObject): CloudTripSummary? {
    val trip = j.optJSONObject("trip")
    val mode = j.optJSONObject("operation")?.optString("mode")
    if (trip?.optString("status") != "ACTIVE" && mode != "EMPTY_MODE") return null
    val data = j.optJSONObject("latest")?.optJSONObject("data") ?: j
    fun number(key: String): Double? {
        val source = if (j.has(key) && !j.isNull(key)) j else data
        return if (!source.has(key) || source.isNull(key)) null else source.optDouble(key).takeIf { it.isFinite() && it >= 0 }
    }
    return CloudTripSummary(number("trip_distance_km"),number("trip_fuel_l"),number("trip_average_kml"),
        trip?.optString("origin")?.takeIf { it.isNotBlank() },trip?.optString("destination")?.takeIf { it.isNotBlank() },
        trip?.takeIf { !it.isNull("weight_t") }?.optDouble("weight_t")?.takeIf { it.isFinite() },mode)
}

private fun requestLiveJson(url: String, connectTimeout: Int, readTimeout: Int): JSONObject? {
    val c=(URL(url).openConnection() as HttpURLConnection).apply {
        requestMethod="GET";this.connectTimeout=connectTimeout;this.readTimeout=readTimeout
        useCaches=false;setRequestProperty("Accept","application/json");setRequestProperty("Connection","close")
    }
    return try {
        if(c.responseCode !in 200..299)null else c.inputStream.bufferedReader().use { JSONObject(it.readText()) }
    } finally { c.disconnect() }
}

/** Local readings and Cloud trip context are independent; background requests
 * never block local instruments or send commands to the vehicle. */
internal class LiveRepository(
    private val configuredHost: String,
    private val parse: (JSONObject,String)->LiveState,
    private val request: (String,Int,Int)->JSONObject? = ::requestLiveJson,
    private val executor: Executor = ForkJoinPool.commonPool(),
    private val clock: ()->Long = System::currentTimeMillis,
) {
    private val cloudUrl="https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-dashboard-data?key=tbx-public-ro-20260828-7f2c91"
    private var cloudFuture: CompletableFuture<JSONObject?>? = null
    private var cloud: JSONObject? = null
    private var cloudAttempt: Long? = null
    private var cloudReceived=0L
    private var diagnosticFuture: CompletableFuture<Pair<String,List<EcuFault>>?>? = null
    private var diagnosticAttempt: Long? = null
    private var diagnosticsAvailable=false
    private val faults=EcuFaultCache()
    private var currentUid: String? = null
    private var coreBase: String? = null

    fun fetch(): LiveState? {
        val now=clock()
        cloudFuture?.takeIf { it.isDone }?.let { future ->
            cloud=try { future.getNow(null) } catch (_:Exception) { null };cloudReceived=now;cloudFuture=null
        }
        diagnosticFuture?.takeIf { it.isDone }?.let { future ->
            val batch=try { future.getNow(null) } catch (_:Exception) { null }
            diagnosticsAvailable=batch!=null && batch.first==currentUid
            if(diagnosticsAvailable)faults.update(batch!!.first,batch.second)
            diagnosticFuture=null
        }
        if(cloudFuture==null && (cloudAttempt==null || now-cloudAttempt!!>=10000)) {
            cloudAttempt=now;cloudFuture=CompletableFuture.supplyAsync({try { request(cloudUrl,2500,3500)?.takeIf { it.optBoolean("ok",true) } } catch (_:Exception) { null }},executor)
        }
        val local=fetchCore()
        if(local!=null) {
            if(diagnosticFuture==null && (diagnosticAttempt==null || now-diagnosticAttempt!!>=30000)) {
                diagnosticAttempt=now;val base=coreBase!!;val uid=currentUid!!
                diagnosticFuture=CompletableFuture.supplyAsync({fetchDiagnostics(base,uid)},executor)
            }
            val recent=cloud?.takeIf { now-cloudReceived<=30000 }
            val trip=recent?.let(::cloudTripSummary)
            val cloudState=recent?.takeIf { it.optBoolean("online",true) }?.let { parse(it,"CLOUD") }
            val cloudPosition=cloudState?.takeIf { it.latitude!=null && it.longitude!=null && (it.latitude!=0.0 || it.longitude!=0.0) }
            return local.copy(tripDistanceKm=trip?.distance,tripFuelLiters=trip?.fuel,tripAverageKml=trip?.average,
                origin=trip?.origin,destination=trip?.destination,tripWeightT=trip?.weight,operationMode=trip?.mode,
                latitude=local.latitude?:cloudPosition?.latitude,longitude=local.longitude?:cloudPosition?.longitude,
                tripCloudAvailable=recent!=null,ecuFaults=faults.snapshot(),diagnosticsAvailable=diagnosticsAvailable)
        }
        return cloud?.takeIf { now-cloudReceived<=30000 }?.let { j ->
            val trip=cloudTripSummary(j)
            parse(j,"CLOUD").copy(tripDistanceKm=trip?.distance,tripFuelLiters=trip?.fuel,tripAverageKml=trip?.average,
                origin=trip?.origin,destination=trip?.destination,tripWeightT=trip?.weight,operationMode=trip?.mode,
                tripCloudAvailable=true,ecuFaults=faults.snapshot(),diagnosticsAvailable=false)
        }
    }
    private fun fetchCore(): LiveState? {
        val bases=linkedSetOf<String>()
        fun add(host:String) { if(host.isNotBlank())bases+=if(host.startsWith("http://") || host.startsWith("https://"))host.trimEnd('/') else "http://${host.trimEnd('/')}" }
        coreBase?.let(::add);add(configuredHost);add("192.168.4.1");add("truckbox.local")
        for(base in bases)try {
            for(path in listOf("/api/live","/api/state?compact=1")) {
                val j=request(base+path,1200,1800)?:continue
                if(!j.optBoolean("ok",true))continue
                if(j.optBoolean("diagnostic_partial") && !j.has("live_contract"))continue
                val uid=j.optString("device_uid").takeIf { it.isNotBlank() }?:continue
                if(uid!=currentUid) { currentUid=uid;faults.update(uid,emptyList());diagnosticAttempt=null;diagnosticsAvailable=false }
                coreBase=base;return parse(j,"CORE")
            }
        } catch (_:Exception) { /* Try the next existing host, never issue a control request. */ }
        return null
    }
    private fun fetchDiagnostics(base:String,uid:String): Pair<String,List<EcuFault>>? { return try {
        var offset=0;val result=mutableListOf<EcuFault>()
        for(page in 0 until 4) {
            val j=request("$base/api/diagnostics?offset=$offset",1200,1800)?:return null
            if(!j.optBoolean("ok") || j.optString("device_uid")!=uid)return null
            result+=parseEcuFaults(j);val next=j.optInt("next_offset",32)
            if(next>=32)break
            if(next<=offset)return null
            offset=next
        }
        uid to result
    } catch (_:Exception) { null } }
}
