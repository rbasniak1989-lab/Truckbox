package br.com.truckbox.app.diagnostics

import android.content.Context
import br.com.truckbox.app.preferences.TruckBoxPreferences
import br.com.truckbox.app.transport.WifiNetworkBinder
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/** Additional read-only diagnostic path. Never ACKs, resets ECUs, or enables polling. */
class DiagnosticClient(context: Context, private val prefs: TruckBoxPreferences) {
    private val binder = WifiNetworkBinder(context)
    private val storage = context.getSharedPreferences("truckbox_diagnostics_v1", Context.MODE_PRIVATE)
    private val catalog = context.assets.open("volvo-diagnostics-cache.json").bufferedReader().use { JSONObject(it.readText()) }
    private fun bases() = linkedSetOf(prefs.coreHost, "truckbox.local", "192.168.4.1").filter { it.isNotBlank() }
        .map { if (it.startsWith("http://") || it.startsWith("https://")) it.trimEnd('/') else "http://${it.trimEnd('/')}" }
    private fun request(connection: HttpURLConnection, body: JSONObject? = null): JSONObject? {
        return try {
            connection.connectTimeout = 1200; connection.readTimeout = 2000; connection.useCaches = false
            if (body != null) {
                connection.requestMethod = "POST"; connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json")
                connection.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
            }
            if (connection.responseCode !in 200..299) null else connection.inputStream.bufferedReader().use { reader ->
                val text = reader.readText(); if (text.length > 262144) null else JSONObject(text).takeIf { it.optBoolean("ok") }
            }
        } catch (_: Exception) { null } finally { connection.disconnect() }
    }
    fun local(path: String): JSONObject? {
        for (base in bases()) try { request(binder.openWifiHttp(URL("$base$path")))?.let { return it } } catch (_: Exception) { }
        return null
    }
    private fun description(fault: JSONObject): JSONObject {
        val definitions = catalog.getJSONArray("definitions")
        for (i in 0 until definitions.length()) {
            val d = definitions.getJSONObject(i)
            if (d.optInt("mid") == fault.optInt("mid") && d.optInt("identifier") == fault.optInt("identifier") &&
                d.optString("identifier_type") == fault.optString("identifier_type") && d.optInt("fmi") == fault.optInt("fmi")) return d
        }
        return JSONObject(catalog.getJSONObject("unknown_policy").toString()).apply {
            put("module", catalog.getJSONObject("modules").optString(fault.optInt("mid").toString(), "Módulo MID ${fault.optInt("mid")}"))
            put("generic_fmi_pt", catalog.getJSONObject("fmi").optString(fault.optInt("fmi").toString(), "FMI desconhecido"))
            put("confidence", "UNKNOWN")
        }
    }
    fun snapshot(tripUid: String? = null, beforeId: String? = null, faultOffset: Int = 0): JSONObject {
        val localFaults = JSONArray(); var offset = 0; var coreUid: String? = null
        if (tripUid == null && beforeId == null) for (pass in 0 until 4) {
            val page = local("/api/diagnostics?offset=$offset") ?: break
            coreUid = page.optString("device_uid").takeIf { it.isNotBlank() }
            val faults = page.optJSONArray("faults") ?: JSONArray()
            for (i in 0 until faults.length()) localFaults.put(faults.getJSONObject(i).apply { put("definition", description(this)) })
            offset = page.optInt("next_offset", 32)
            if (offset >= 32) break
        }
        val body = JSONObject().put("device_uid", prefs.cloudDeviceUid).put("token", prefs.cloudDeviceToken)
        tripUid?.let { body.put("trip_client_uid", it) }; beforeId?.let { body.put("before_id", it) }; body.put("fault_offset", faultOffset)
        coreUid?.let { body.put("core_device_uid", it) }
        val remote = if (prefs.cloudDeviceUid.isNotBlank() && prefs.cloudDeviceToken.isNotBlank())
            request(URL("https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-diagnostics").openConnection() as HttpURLConnection, body) else null
        if (remote != null && coreUid != null && remote.optBoolean("vehicle_matches_core"))
            storage.edit().putString("vehicle_of_core:$coreUid", remote.optString("truck_id")).apply()
        if (remote != null) storage.edit().putString("vehicle_of_credentials:${prefs.cloudDeviceUid}", remote.optString("truck_id")).apply()
        val scope = remote?.optString("truck_id")?.takeIf { it.isNotBlank() }
            ?: coreUid?.let { storage.getString("vehicle_of_core:$it", null) ?: "core:$it" }
            ?: storage.getString("vehicle_of_credentials:${prefs.cloudDeviceUid}", "cloud:${prefs.cloudDeviceUid}")
        val cacheKey = "faults:$scope:${tripUid.orEmpty()}"
        val cached = storage.getString(cacheKey, null)?.let { runCatching { JSONObject(it) }.getOrNull() }
        val result = remote ?: cached ?: JSONObject().put("ok", false).put("error", "DIAGNOSTICS_NOT_AVAILABLE").put("faults", JSONArray())
        if (remote != null && cached != null)
            result.put("faults", DiagnosticMerge.faults(cached.optJSONArray("faults") ?: JSONArray(), result.optJSONArray("faults") ?: JSONArray()))
        if (localFaults.length() > 0 && (remote == null || remote.optBoolean("vehicle_matches_core"))) {
            val previous = result.optJSONArray("faults") ?: JSONArray()
            result.put("ok", true).put("faults", DiagnosticMerge.faults(previous, localFaults)).put("source", "CORE_WITH_CACHED_HISTORY")
        } else result.put("source", if (remote != null) "CLOUD" else "CACHE")
        result.put("history_complete", false)
        // Current page can contain128 faults; the persistent cache keeps64.
        val cacheResult = JSONObject(result.toString())
        val all = cacheResult.optJSONArray("faults") ?: JSONArray()
        if (all.length() > 64) {
            result.put("cache_limited", true)
            cacheResult.put("cache_limited", true).put("faults", JSONArray((0 until 64).map { all.getJSONObject(it) }))
        }
        val text = cacheResult.toString()
        if (result.optBoolean("ok") && text.length <= 262144) synchronized(lock) {
            val oldKeys = runCatching { JSONArray(storage.getString("cache_keys", "[]")) }.getOrDefault(JSONArray())
            val keys = listOf(cacheKey) + (0 until oldKeys.length()).map { oldKeys.optString(it) }.filter { it != cacheKey }
            val edit = storage.edit().putString(cacheKey, text).putString("cache_keys", JSONArray(keys.take(8)).toString())
            keys.drop(8).forEach { edit.remove(it) }; edit.apply()
        }
        return result
    }
    fun enqueueBoundary(start: Boolean, uid: String, atMs: Long) = synchronized(lock) {
        val q = JSONArray(storage.getString("boundaries", "[]")); val event = if (start) "START" else "END"
        if ((0 until q.length()).any { q.getJSONObject(it).optString("trip_client_uid") == uid && q.getJSONObject(it).optString("event") == event }) return@synchronized true
        if (q.length() >= 64) return@synchronized false
        q.put(JSONObject().put("event", event).put("trip_client_uid", uid).put("requested_unix_ms", atMs))
        storage.edit().putString("boundaries", q.toString()).commit()
    }
    fun flushOneBoundary() {
        val item = synchronized(lock) {
            if (flushing) return
            val q = JSONArray(storage.getString("boundaries", "[]")); if (q.length() == 0) return
            flushing = true; q.getJSONObject(0)
        }
        try {
            val delay = System.currentTimeMillis() - item.optLong("requested_unix_ms")
            for (base in bases()) {
                val payload = JSONObject(item.toString()).apply { put("queue_age_ms", if (delay in 0..4294967294L) delay else 4294967295L) }
                val reply = try { request(binder.openWifiHttp(URL("$base/api/counters/capture")), payload) } catch (_: Exception) { null }
                if (reply?.optBoolean("durable") == true) {
                    synchronized(lock) {
                        val q = JSONArray(storage.getString("boundaries", "[]"))
                        if (q.length() > 0 && q.getJSONObject(0).toString() == item.toString())
                            storage.edit().putString("boundaries", JSONArray((1 until q.length()).map { q.getJSONObject(it) }).toString()).commit()
                    }
                    return
                }
            }
        } finally { synchronized(lock) { flushing = false } }
    }
    companion object { private val lock = Any(); private var flushing = false }
}
