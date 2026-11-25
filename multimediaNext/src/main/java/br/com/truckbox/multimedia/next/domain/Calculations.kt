package br.com.truckbox.multimedia.next.domain

import java.time.Instant

data class InstantConsumption(val value: Double, val unit: String)
fun instantConsumption(speed: Reading?, flow: Reading?, now: Instant): InstantConsumption? {
    val s = speed?.current(now) ?: return null
    val f = flow?.current(now) ?: return null
    if (s < 0 || f < 0) return null
    return if (s == 0.0) InstantConsumption(f, "L/h") else if (f > 0) InstantConsumption(s / f, "km/l") else null
}

data class FuelDisplay(val value: Double?, val unit: String, val explanation: String)

fun displayedConsumption(speed: Reading?, rate: Reading?, window: LocalFuelWindow?, now: Instant): FuelDisplay? {
    val kmh = speed?.observed(now)?.takeIf { it >= 0 }
    val lph = rate?.observed(now)?.takeIf { it >= 0 && (it > 0 || rate.quality == Quality.VALID) }
    if (lph != null) {
        val oldest = if (kmh != null && speed.sampledAt != null) minOf(requireNotNull(rate.sampledAt), speed.sampledAt)
            else requireNotNull(rate.sampledAt)
        val age = java.time.Duration.between(oldest, now).seconds
        val source = if (rate.sourceId?.endsWith("/PID:183") == true) "PID 183" else rate.sourceId ?: "Vazão local"
        val motionSource = if (speed?.sourceId?.endsWith("/PID:84") == true) "PID 84" else speed?.sourceId ?: "velocidade local"
        val origin = "$source · ${if (rate.quality == Quality.PROVISIONAL || speed?.quality == Quality.PROVISIONAL) "provisório" else "medido"} · ${age}s"
        if (lph == 0.0 && kmh != null && kmh > 0 && speed.quality == Quality.VALID) {
            return FuelDisplay(null, "km/L", "Corte de combustível · $origin")
        }
        return if (kmh != null && kmh > 0 && lph > 0) FuelDisplay(kmh / lph, "km/L", "$origin · $motionSource")
        else FuelDisplay(lph, "L/h", if (kmh == null) "$origin · sem velocidade atual" else origin)
    }
    val complete = window?.takeIf { it.fresh(now) } ?: return null
    val mean = complete.averageLph ?: return null
    val age = java.time.Duration.between(complete.endedAt, now).seconds
    return FuelDisplay(mean, "L/h", "Média ponderada PID 183 · 10 s · ${age}s atrás")
}

data class TimeSample(val id: String, val at: Instant, val value: Double?, val quality: Quality, val sourceId: String)
data class TimeStats(val min: Double, val mean: Double, val max: Double, val coverage: Double, val samples: Int)
/** Degrau constante à esquerda, sem carregar leitura além de maxGapSeconds.
 * Amostras inválidas interrompem a continuidade, em vez de serem atravessadas. */
fun timeStats(samples: List<TimeSample>, start: Instant, end: Instant, maxGapSeconds: Long = 10, minimumCoverage: Double = .7): TimeStats? {
    val total = end.toEpochMilli() - start.toEpochMilli()
    if (total <= 0 || maxGapSeconds <= 0 || minimumCoverage !in 0.0..1.0) return null
    val unique = samples.groupBy { it.id }
    if (unique.values.any { it.distinct().size > 1 }) return null
    val ordered = unique.values.map { it.first() }.sortedBy { it.at }
    if (ordered.zipWithNext().any { (a,b) -> a.at == b.at && a.value != b.value }) return null
    val points = ordered.distinctBy { it.at }
    var covered = 0L; var integral = 0.0
    val observed = mutableListOf<Double>()
    points.forEachIndexed { i, p ->
        if (p.quality != Quality.VALID || p.value == null || !p.value.isFinite() || p.sourceId.isBlank()) return@forEachIndexed
        if (p.at >= start && p.at <= end) observed += p.value
        val from = maxOf(start.toEpochMilli(), p.at.toEpochMilli())
        val to = minOf(end.toEpochMilli(), points.getOrNull(i+1)?.at?.toEpochMilli() ?: end.toEpochMilli(), p.at.toEpochMilli()+maxGapSeconds*1000)
        if (to > from) { covered += to-from; integral += (to-from)*p.value }
    }
    val coverage = covered.toDouble()/total
    if (covered == 0L || coverage < minimumCoverage || observed.size < 2) return null
    return TimeStats(observed.min(), integral/covered, observed.max(), coverage, observed.size)
}

enum class FaultState { ACTIVE_CONFIRMED, INACTIVE_CONFIRMED, UNKNOWN }
data class FaultEpisode(
    val id: String, val code: String, val source: String, val activatedAt: Instant,
    val lastSeenActive: Instant, val deactivatedAt: Instant? = null,
    val state: FaultState = FaultState.ACTIVE_CONFIRMED,
) {
    fun lostCommunication() = copy(state = FaultState.UNKNOWN)
    fun confirmedInactive(at: Instant): FaultEpisode {
        require(at >= lastSeenActive)
        return copy(deactivatedAt = at, state = FaultState.INACTIVE_CONFIRMED)
    }
}

enum class JourneyKind { DRIVING, REST_CONFIRMED, WAITING, OTHER_STOP, UNKNOWN }
data class JourneySegment(val start: Instant, val end: Instant, val kind: JourneyKind, val confirmed: Boolean = false)
data class JourneySummary(val driving: Long, val rest: Long, val waiting: Long, val otherStop: Long, val unknown: Long) {
    val stopped get() = rest + waiting + otherStop
    val total get() = driving + stopped + unknown
}
/** Últimas 24 h; sobreposições contraditórias e descanso não confirmado viram desconhecido. */
fun journey24h(segments: List<JourneySegment>, reference: Instant): JourneySummary {
    val start = reference.minusSeconds(86400)
    val valid = segments.filter { it.end > it.start && it.end > start && it.start < reference }.distinct()
    val cuts = (valid.flatMap { listOf(maxOf(it.start,start), minOf(it.end,reference)) } + listOf(start,reference)).distinct().sorted()
    val seconds = JourneyKind.entries.associateWith { 0L }.toMutableMap()
    cuts.zipWithNext().forEach { (a,b) ->
        val kinds = valid.filter { it.start <= a && it.end >= b }.map {
            if (it.kind == JourneyKind.REST_CONFIRMED && !it.confirmed) JourneyKind.UNKNOWN else it.kind
        }.distinct()
        val kind = kinds.singleOrNull() ?: JourneyKind.UNKNOWN
        seconds[kind] = seconds.getValue(kind) + (b.epochSecond-a.epochSecond)
    }
    return JourneySummary(seconds.getValue(JourneyKind.DRIVING),seconds.getValue(JourneyKind.REST_CONFIRMED),seconds.getValue(JourneyKind.WAITING),seconds.getValue(JourneyKind.OTHER_STOP),seconds.getValue(JourneyKind.UNKNOWN))
}

enum class TollStatus { FORECAST, IDENTIFIED, CONFIRMED, REVIEW }
enum class TollPayer { OWNER, CARRIER, REIMBURSABLE }
data class TollEvent(val passageKey: String, val revision: Long, val legId: String, val status: TollStatus, val payer: TollPayer, val amountCents: Long?)
data class TollProjection(val ownCents: Long, val reimbursableCents: Long, val hasUnknownPrice: Boolean)
fun tollProjection(events: List<TollEvent>, legId: String): TollProjection {
    var conflict = false
    val latest = events.filter { it.legId == legId }.groupBy { it.passageKey }.values.mapNotNull { group ->
        val revision = group.maxOf { it.revision }
        val candidates = group.filter { it.revision == revision }.distinct()
        if(candidates.size != 1 || revision <= 0 || candidates[0].passageKey.isBlank() || candidates[0].amountCents?.let { it < 0 } == true) {
            conflict = true; null
        } else candidates.single()
    }
    return TollProjection(
        latest.filter { it.payer == TollPayer.OWNER }.sumOf { it.amountCents ?: 0 },
        latest.filter { it.payer == TollPayer.REIMBURSABLE }.sumOf { it.amountCents ?: 0 },
        conflict || latest.any { it.amountCents == null || it.status == TollStatus.REVIEW },
    )
}
