package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.*

/** Cache técnico volátil por Core. Só conserva números reais acompanhados de origem/captura. */
fun Reading.canBeLastInstrumentValue() = value?.isFinite()==true && sampledAt!=null && !sourceId.isNullOrBlank() &&
    source==Origin.CORE && quality in setOf(Quality.VALID,Quality.PROVISIONAL,Quality.STALE)

fun retainCoreReadings(previous: Map<Instrument,InstrumentValue>,incoming: Map<Instrument,InstrumentValue>): Map<Instrument,InstrumentValue> {
    fun keep(old: Reading?,next: Reading?): Reading? = when {
        old?.canBeLastInstrumentValue()==true && next?.canBeLastInstrumentValue()==true &&
            old.sourceId==next.sourceId && old.bootId==next.bootId && old.sampledAt!!.isAfter(next.sampledAt) -> old.copy(quality=Quality.STALE)
        next?.canBeLastInstrumentValue()==true -> next
        old?.canBeLastInstrumentValue()==true -> old.copy(quality=Quality.STALE)
        else -> next
    }
    return Instrument.defaults.mapNotNull {sensor->
        val next=incoming[sensor]
        var old=previous[sensor]
        // Nunca completar o cartão de ar com um circuito de outra ECU ou outro boot.
        if(sensor==Instrument.AIR) {
            val sources=listOfNotNull(next?.first,next?.second).filter {it.canBeLastInstrumentValue()}
            val previousSources=listOfNotNull(old?.first,old?.second).filter {it.canBeLastInstrumentValue()}
            if(sources.any {n->previousSources.any {p->n.sourceId?.substringBefore("/PID")!=p.sourceId?.substringBefore("/PID") || n.bootId!=p.bootId}})old=null
        }
        val first=keep(old?.first,next?.first)
        val second=keep(old?.second,next?.second)
        if(first==null && second==null)null
        else sensor to InstrumentValue(first ?: Reading(unit=sensor.unit,source=Origin.CORE),second)
    }.toMap()
}
fun staleCoreReadings(readings: Map<Instrument,InstrumentValue>)=retainCoreReadings(readings,emptyMap())
