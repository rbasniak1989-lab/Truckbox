package br.com.truckbox.app.cloud

import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.time.Instant
import java.time.OffsetDateTime
import java.time.format.DateTimeFormatter

enum class DriverEndpoint(val path: String) { OPERATIONS("truckbox-app"),GPS("truckbox-gps") }
fun interface DriverConnectionFactory {fun open(url: URL): HttpURLConnection}
class DriverCloudException(val status: Int?=null): IOException("Cloud request unavailable")

/** Only fixed HTTPS Cloud endpoints. No LAN, Wi-Fi binding, redirects or raw error payloads. */
class DriverCloudTransport(private val connections: DriverConnectionFactory=DriverConnectionFactory {it.openConnection() as HttpURLConnection}) {
    fun post(endpoint: DriverEndpoint,body: JSONObject,readTimeoutMs: Int=8000): JSONObject {
        val url=URL("https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/${endpoint.path}")
        val c=connections.open(url)
        try {
            c.requestMethod="POST";c.connectTimeout=5000;c.readTimeout=readTimeoutMs
            c.useCaches=false;c.instanceFollowRedirects=false;c.doOutput=true
            c.setRequestProperty("Content-Type","application/json");c.setRequestProperty("Accept","application/json")
            c.setRequestProperty("Cache-Control","no-store");c.setRequestProperty("Connection","close")
            val bytes=body.toString().toByteArray(Charsets.UTF_8);c.setFixedLengthStreamingMode(bytes.size)
            c.outputStream.use {it.write(bytes)}
            val status=c.responseCode
            if(status !in 200..299)throw DriverCloudException(status)
            val out=java.io.ByteArrayOutputStream();val buffer=ByteArray(4096)
            c.inputStream.use {input->while(out.size()<=524288){val n=input.read(buffer);if(n<0)break;out.write(buffer,0,n)}}
            if(out.size()==0||out.size()>524288)throw DriverCloudException(502)
            return JSONObject(out.toString(Charsets.UTF_8.name())).also {if(!it.optBoolean("ok",false))throw DriverCloudException(502)}
        } finally {c.disconnect()}
    }
}

/** OffsetDateTime supports PostgreSQL +00:00 on Android 8–13, unlike old Instant.parse. */
fun cloudInstant(value: String): Instant=OffsetDateTime.parse(value,DateTimeFormatter.ISO_OFFSET_DATE_TIME).toInstant()
