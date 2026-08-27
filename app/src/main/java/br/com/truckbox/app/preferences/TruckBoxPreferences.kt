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

class TruckBoxPreferences(context: Context) {
    private val p = context.getSharedPreferences("truckbox", Context.MODE_PRIVATE)

    var accent: AccentOption
        get() = runCatching { AccentOption.valueOf(p.getString("accent", AccentOption.BLUE.name)!!) }.getOrDefault(AccentOption.BLUE)
        set(v) { p.edit().putString("accent", v.name).apply() }

    var developerMode: Boolean
        get() = p.getBoolean("developer_mode", true)
        set(v) { p.edit().putBoolean("developer_mode", v).apply() }

    /**
     * Na rede Starlink o Core é descoberto por mDNS. Se a multimídia/roteador não
     * resolver .local, o usuário pode informar o IP LAN do ESP32 aqui.
     */
    var coreHost: String
        get() = p.getString("core_host", "truckbox.local") ?: "truckbox.local"
        set(v) { p.edit().putString("core_host", v.trim().ifBlank { "truckbox.local" }).apply() }

    var cloudDeviceUid: String
        get() = p.getString("cloud_device_uid", "TBX-FH540-0001") ?: "TBX-FH540-0001"
        set(v) { p.edit().putString("cloud_device_uid", v.trim()).apply() }

    var cloudDeviceToken: String
        get() = p.getString("cloud_device_token", "") ?: ""
        set(v) { p.edit().putString("cloud_device_token", v.trim()).apply() }

    var gpsDeviceUid: String
        get() = p.getString("gps_device_uid", "TBX-FH540-GPS01") ?: "TBX-FH540-GPS01"
        set(v) { p.edit().putString("gps_device_uid", v.trim()).apply() }

    var gpsDeviceToken: String
        get() = p.getString("gps_device_token", "") ?: ""
        set(v) { p.edit().putString("gps_device_token", v.trim()).apply() }

    var gatewayEnabled: Boolean
        get() = p.getBoolean("gateway_enabled", true)
        set(v) { p.edit().putBoolean("gateway_enabled", v).apply() }

    val installId: String
        get() = p.getString("install_id", null) ?: java.util.UUID.randomUUID().toString().also {
            p.edit().putString("install_id", it).apply()
        }
}
