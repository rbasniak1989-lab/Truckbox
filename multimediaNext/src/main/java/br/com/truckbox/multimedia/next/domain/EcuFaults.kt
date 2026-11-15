package br.com.truckbox.multimedia.next.domain

import com.google.gson.JsonObject
import java.time.Instant

/** Somente DTCs efetivamente retornados pelo Core. Silêncio nunca resolve falhas. */
enum class EcuFaultState { ACTIVE, INACTIVE, UNKNOWN }

data class EcuFault(
    val mid: Int,
    val identifierType: String,
    val identifier: Int,
    val fmi: Int,
    val page2: Boolean,
    val state: EcuFaultState,
    val occurrences: Int?,
    val firstSeen: Instant?,
    val lastSeen: Instant?,
    val rawHex: String,
    val quality: String,
) {
    val key: String get() = "$" + "{mid}:$" + "{identifierType}:$" + "{identifier}:$" + "{fmi}:$" + "{page2}"
    val technicalCode: String get() = "MID $" + "{mid} • $" + "{identifierType} $" + "{identifier} • FMI $" + "{fmi}" + (if(page2) " · pág. 2" else "")
}

data class EcuFaultSnapshot(
    val uid: String,
    val checkedAt: Instant,
    val faults: List<EcuFault>,
    val incomplete: Boolean,
    val overflow: Int,
)

/** Leitor de um contrato conhecido; nunca tenta inferir códigos de payload de sensores. */
fun parseFaultEntries(page: JsonObject): List<EcuFault> {
    val list=page.getAsJsonArray("faults") ?: return emptyList()
    val result=mutableListOf<EcuFault>()
    for(item in list) {
        val j=runCatching { item.asJsonObject }.getOrNull() ?: continue
        fun number(key:String): Int? = runCatching { j.get(key)?.takeUnless { it.isJsonNull }?.asInt }.getOrNull()
        fun word(key:String): String? = runCatching { j.get(key)?.takeUnless { it.isJsonNull }?.asString }.getOrNull()
        fun moment(key:String): Instant? = runCatching {
            val times=j.getAsJsonArray(key) ?: return@runCatching null
            if(times.size()<3 || times[2].isJsonNull)return@runCatching null
            val ms=times[2].asLong
            if(ms<1577836800000L || ms>4102444800000L) null else Instant.ofEpochMilli(ms)
        }.getOrNull()
        val mid=number("mid") ?: continue
        val id=number("identifier") ?: continue
        val fmi=number("fmi") ?: continue
        val type=word("identifier_type") ?: continue
        if(mid !in 128..255 || id !in 0..511 || fmi !in 0..15 || type !in setOf("PID","SID","PPID","PSID"))continue
        val state=when(word("state")) {"ACTIVE"->EcuFaultState.ACTIVE;"INACTIVE"->EcuFaultState.INACTIVE;else->EcuFaultState.UNKNOWN}
        result+=EcuFault(mid,type,id,fmi,runCatching {j.get("page2")?.asBoolean==true}.getOrDefault(false),
            state,number("occurrences")?.takeIf {it>=0},moment("first"),moment("last"),
            word("parameter_hex")?.takeIf {it.length<=24 && it.matches(Regex("[A-Fa-f0-9]*")) }.orEmpty(),
            word("quality").orEmpty())
    }
    return result
}

/** "Ativa agora" só quando houve relato recente da ECU e comunicação íntegra. */
fun EcuFault.confirmedActive(now: Instant,scanAt: Instant?,coreAvailable:Boolean):Boolean =
    state==EcuFaultState.ACTIVE && coreAvailable && scanAt!=null &&
    !scanAt.isAfter(now.plusSeconds(5)) && java.time.Duration.between(scanAt,now).seconds in 0..40 &&
    lastSeen!=null && java.time.Duration.between(lastSeen,now).seconds in 0..120

/** Continua relevante na Home mesmo sem confirmação recente, mas nunca fica rotulada como "atual". */
fun EcuFault.pendingActive():Boolean = state==EcuFaultState.ACTIVE
