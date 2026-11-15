package br.com.truckbox.multimedia

import org.json.JSONObject

/** Display ECU reports; absence from a later page is never a resolution. */
internal data class EcuFault(
    val mid: Int,
    val identifierType: String,
    val identifier: Int,
    val fmi: Int,
    val state: String,
    val occurrences: Int?,
    val raw: String,
    val lastBoot: Long?,
    val lastUptimeMs: Long?,
    val lastUnixMs: Long?,
    val page2: Boolean,
) {
    val key: String get() = "$mid:$identifierType:$identifier:$fmi"
    fun technicalCode(): String = "MID $mid • $identifierType $identifier${if(page2) " (página 2)" else ""} • FMI $fmi"
    fun stateText(): String = when (state) { "ACTIVE" -> "ATIVA"; "INACTIVE" -> "INATIVA"; else -> "ESTADO DESCONHECIDO" }
    fun candidateDescription(): String? = when {
        mid == 128 && identifierType == "SID" && identifier == 18 && fmi == 5 ->
            "Correspondência candidata: circuito do sensor de água no combustível, corrente baixa/aberto. Não confirma água no diesel."
        mid == 136 && identifierType == "SID" && identifier == 2 && fmi == 5 ->
            "Correspondência candidata: circuito do sensor da roda dianteira direita; depende da configuração ABS."
        mid == 130 && identifierType == "PID" && identifier == 37 && fmi == 1 ->
            "Pressão de ar da transmissão abaixo da faixa operacional; descrição em validação."
        else -> null
    }
}

internal fun parseEcuFaults(page: JSONObject): List<EcuFault> {
    val rows = page.optJSONArray("faults") ?: return emptyList()
    fun JSONObject.optionalLong(name: String): Long? = if (has(name) && !isNull(name)) optLong(name) else null
    return (0 until rows.length()).mapNotNull { i ->
        val row = rows.optJSONObject(i) ?: return@mapNotNull null
        val mid = row.optInt("mid", -1)
        val number = row.optInt("identifier", -1)
        val fmi = row.optInt("fmi", -1)
        val type = row.optString("identifier_type", "")
        if (mid !in 128..255 || number !in 0..511 || fmi !in 0..15 || type !in setOf("PID", "SID", "PPID", "PSID")) return@mapNotNull null
        val last = row.optJSONArray("last")
        EcuFault(mid, type, number, fmi, row.optString("state", "UNKNOWN"),
            row.optionalLong("occurrences")?.toInt(), row.optString("parameter_hex", ""),
            last?.takeIf { !it.isNull(0) }?.optLong(0), last?.takeIf { !it.isNull(1) }?.optLong(1),
            last?.takeIf { !it.isNull(2) }?.optLong(2), row.optBoolean("page2", false))
    }
}

internal class EcuFaultCache {
    private var uid: String? = null
    private val entries = linkedMapOf<String, EcuFault>()
    fun update(deviceUid: String, faults: List<EcuFault>): List<EcuFault> {
        if (uid != deviceUid) { entries.clear(); uid = deviceUid }
        faults.forEach { entries[it.key] = it }
        while (entries.size > 32) entries.remove(entries.keys.first())
        return entries.values.toList()
    }
    fun snapshot(): List<EcuFault> = entries.values.toList()
}
