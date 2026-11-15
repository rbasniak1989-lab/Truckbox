package br.com.truckbox.app

import android.content.Context
import br.com.truckbox.app.cloud.*
import br.com.truckbox.app.gps.PhoneGpsSample
import br.com.truckbox.app.preferences.*
import org.json.JSONObject
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.Base64

internal const val VEHICLE = "00000000-0000-4000-8000-000000000001"
internal const val TEST_TIME = 1794657600123L
internal val TEST_CREDENTIALS = DriverCredentials("TEST-DRIVER", "f".repeat(64), VEHICLE)
internal fun sample(time: Long = TEST_TIME) = PhoneGpsSample("test-install:$time", time, -25.0, -49.0, provider = "gps")
internal class MemoryTokens : DriverTokens {
    val entries = mutableMapOf<String, String>()
    var failWrites = false
    override fun read(name: String) = entries[name]
    override fun write(name: String, value: String) { check(!failWrites); entries[name] = value }
}
internal fun code(uid: String = TEST_CREDENTIALS.deviceUid, vehicle: String = VEHICLE, token: String = TEST_CREDENTIALS.token): String =
    "tbxd1_" + Base64.getUrlEncoder().withoutPadding().encodeToString(JSONObject()
        .put("version", 1).put("deviceUid", uid).put("vehicleId", vehicle).put("token", token).toString().toByteArray())
internal fun prefs(context: Context) = TruckBoxPreferences(context, MemoryTokens()).apply { importCloudCode(code()) }

internal class FakeCloud(var reply: (URL, JSONObject) -> Pair<Int, String>) : DriverConnectionFactory {
    val requests = mutableListOf<Pair<URL, JSONObject>>()
    val connections = mutableListOf<HttpURLConnection>()
    override fun open(url: URL): HttpURLConnection = object : HttpURLConnection(url) {
        val output = ByteArrayOutputStream()
        private var response: Pair<Int, String>? = null
        override fun connect() {}
        override fun disconnect() { connected = false }
        override fun usingProxy() = false
        override fun getOutputStream() = output
        override fun getResponseCode(): Int {
            if (response == null) {
                val body = JSONObject(output.toString("UTF-8"))
                requests += url to body
                response = reply(url, body)
            }
            return response!!.first
        }
        override fun getInputStream() = ByteArrayInputStream(response!!.second.toByteArray())
    }.also { connections += it }
    fun transport() = DriverCloudTransport(this)
}
internal fun gpsAck(body: JSONObject, vehicle: String = VEHICLE, time: String = body.getString("ts")) =
    200 to JSONObject().put("ok", true).put("truck_id", vehicle).put("accepted_ts", time).toString()
