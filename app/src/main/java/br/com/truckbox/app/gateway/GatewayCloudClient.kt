package br.com.truckbox.app.gateway

import br.com.truckbox.app.cloud.*
import br.com.truckbox.app.gps.PhoneGpsSample
import br.com.truckbox.app.preferences.DriverCredentials
import org.json.JSONObject

/** Component name kept for source compatibility; this client sends phone GPS only. */
class GatewayCloudClient(private val transport: DriverCloudTransport=DriverCloudTransport()) {
    fun uploadGps(sample: PhoneGpsSample,credentials: DriverCredentials): Boolean {
        require(sample.valid()&&credentials.configured)
        val body=JSONObject().apply {
            put("device_uid",credentials.deviceUid);put("token",credentials.token);put("source","ANDROID_PHONE")
            put("client_uid",sample.clientUid);put("ts",sample.timestamp);put("lat",sample.latitude);put("lon",sample.longitude)
            // Legacy API converts JSON null to numeric zero. Omission preserves SQL NULL.
            sample.speedKmh?.let {put("speed_kmh",it)};sample.headingDeg?.let {put("heading_deg",it)}
            sample.altitudeM?.let {put("altitude_m",it)};sample.accuracyM?.let {put("accuracy_m",it)};put("provider",sample.provider)
        }
        val ack=transport.post(DriverEndpoint.GPS,body)
        val truck=ack.optString("truck_id").takeIf {it.isNotBlank()} ?: return false
        if(credentials.vehicleId!=null&&truck!=credentials.vehicleId)return false
        return runCatching {cloudInstant(ack.getString("accepted_ts")).toEpochMilli()==sample.capturedAtMs}.getOrDefault(false)
    }
}
