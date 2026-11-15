package br.com.truckbox.app.preferences

import android.content.Context

enum class AccentOption(val label: String, val argb: Long) {
    BLUE("Azul", 0xFF2196F3),
    GREEN("Verde", 0xFF22D760),
    RED("Vermelho", 0xFFFF3B45),
    ORANGE("Laranja", 0xFFFF9800),
    PURPLE("Roxo", 0xFF9B5CFF),
    CYAN("Ciano", 0xFF00BCD4),
    YELLOW("Amarelo", 0xFFFFC928),
}

data class DriverCredentials(val deviceUid: String,val token: String,val vehicleId: String?) {
    val configured: Boolean get()=deviceUid.isNotBlank()&&token.isNotBlank()
    val scope: String get()=deviceUid+"|"+(vehicleId ?: "legacy")
}
class TruckBoxPreferences(context: Context,private val tokens: DriverTokens=DriverTokenStore(context)) {
    companion object {private val credentialsLock=Any()}
    private val p = context.getSharedPreferences("truckbox", Context.MODE_PRIVATE)

    var accent: AccentOption
        get() = runCatching { AccentOption.valueOf(p.getString("accent", AccentOption.BLUE.name)!!) }.getOrDefault(AccentOption.BLUE)
        set(v) { p.edit().putString("accent", v.name).apply() }

    var developerMode: Boolean
        get() = p.getBoolean("developer_mode", true)
        set(v) { p.edit().putBoolean("developer_mode", v).apply() }

    var cloudDeviceUid: String
        get() = p.getString("cloud_device_uid", "TBX-FH540-0001") ?: "TBX-FH540-0001"
        set(v) { p.edit().putString("cloud_device_uid", v.trim()).apply() }

    var cloudDeviceToken: String
        get() = readToken("cloud_device_token")
        set(v) {writeToken("cloud_device_token",v)}

    var gpsDeviceUid: String
        get() = p.getString("gps_device_uid", "TBX-FH540-GPS01") ?: "TBX-FH540-GPS01"
        set(v) { p.edit().putString("gps_device_uid", v.trim()).apply() }

    var gpsDeviceToken: String
        get() = readToken("gps_device_token")
        set(v) {writeToken("gps_device_token",v)}

    private fun readToken(name: String): String=synchronized(credentialsLock) {
        p.getString("active_token_key",null)?.let {key->return@synchronized requireNotNull(tokens.read(key))}
        tokens.read(name)?.let {return@synchronized it}
        val legacy=p.getString(name,"").orEmpty()
        if(legacy.isNotEmpty()) {tokens.write(name,legacy);check(p.edit().remove(name).commit())}
        legacy
    }
    private fun writeToken(name: String,value: String)=synchronized(credentialsLock) {
        tokens.write(name,value.trim());check(p.edit().remove(name).commit())
    }
    fun cloudCredentials()=synchronized(credentialsLock) {DriverCredentials(cloudDeviceUid,cloudDeviceToken,p.getString("cloud_vehicle_id",null))}
    fun gpsCredentials()=synchronized(credentialsLock) {DriverCredentials(gpsDeviceUid,gpsDeviceToken,p.getString("cloud_vehicle_id",null))}
    fun importCloudCode(code: String)=synchronized(credentialsLock) {
        require(code.startsWith("tbxd1_")&&code.length<=4096)
        val j=org.json.JSONObject(String(java.util.Base64.getUrlDecoder().decode(code.removePrefix("tbxd1_")),Charsets.UTF_8))
        require(j.getInt("version")==1)
        val uid=j.getString("deviceUid");val token=j.getString("token");val vehicle=j.getString("vehicleId")
        require(Regex("[A-Za-z0-9_-]{1,128}").matches(uid)&&Regex("[0-9a-f]{64}").matches(token))
        java.util.UUID.fromString(vehicle)
        // One owner/vehicle field test. A future multi-account flow must isolate local data first.
        require(p.getString("cloud_vehicle_id",null).let {it==null||it==vehicle})
        val key="access_${uid}_$vehicle"
        tokens.write(key,token)
        check(p.edit().putString("cloud_device_uid",uid).putString("gps_device_uid",uid).putString("cloud_vehicle_id",vehicle)
            .putString("active_token_key",key).remove("cloud_device_token").remove("gps_device_token").commit())
    }

    var gpsTrackingEnabled: Boolean
        get() = p.getBoolean("gps_tracking_enabled",true)
        set(v) {p.edit().putBoolean("gps_tracking_enabled",v).apply()}

    var gatewayEnabled: Boolean
        get() = p.getBoolean("gateway_enabled", true)
        set(v) { p.edit().putBoolean("gateway_enabled", v).apply() }

    val installId: String
        get() = p.getString("install_id", null) ?: java.util.UUID.randomUUID().toString().also {
            check(p.edit().putString("install_id", it).commit())
        }
}
