package br.com.truckbox.multimedia.next.domain

import java.time.Duration
import java.time.Instant

data class FuelRange(
    val capacityLiters: Double?,
    val remainingLiters: Double?,
    val percent: Double?,
    val fullTankAt: Instant?,
    val fullTankOdometerKm: Double?,
    val consumedSinceFullLiters: Double?,
    val partialAddedLiters: Double?,
    val averageKml: Double?,
    val averageSource: String?,
    val autonomyKm: Double?,
    val quality: String,
    val updatedAt: Instant,
) {
    val qualityLabel: String get() = when (quality) {
        "VALID", "CONFIRMED", "RECONCILED" -> "Confirmada pela Cloud"
        "APPROXIMATE", "ESTIMATED" -> "Aproximada"
        "STALE" -> "Telemetria desatualizada"
        "NO_FULL_TANK_REFERENCE" -> "Sem referência de tanque cheio"
        "NO_TELEMETRY_REFERENCE" -> "Sem referência de telemetria"
        "COUNTER_CONFLICT" -> "Inconsistência no contador"
        else -> "Qualidade informada pela Cloud: $quality"
    }
    val averageLabel: String get() = when (averageSource) {
        "ACTIVE_TRIP" -> "Média da viagem"
        "EMPTY_LEG" -> "Média do trecho vazio"
        "LAST_50_KM" -> "Últimos 50 km"
        "LAST_100_KM" -> "Últimos 100 km"
        "SESSION_AVG" -> "Média da sessão"
        else -> "Média da Cloud"
    }
}

data class FuelInterval(
    val fromKm: Double, val toKm: Double, val coveredKm: Double, val liters: Double,
    val idleLiters: Double? = null, val plannedLiters: Double? = null,
    val altitudeM: Double? = null,
    val gapBefore: Boolean = false,
)
data class FuelHistory(
    val throughKm: Double, val sampledAt: Instant, val source: String, val rows: List<FuelInterval>,
    val coreUid: String? = null,
) {
    fun between(fromKm: Double, toKm: Double): List<FuelInterval> =
        rows.filter { it.fromKm >= fromKm && it.toKm <= toKm && it.fromKm < toKm }
}

data class ElevationPoint(val distanceM: Double, val altitudeM: Double)
data class RoutePosition(val latitude: Double, val longitude: Double)
data class RouteToll(val name: String, val position: RoutePosition?)
data class JourneyRoute(
    val id: String, val tripId: String, val revision: Int?, val distanceM: Double,
    val points: List<RoutePosition>, val tolls: List<RouteToll>, val validUntil: Instant?,
    val remainingM: Double? = null, val progressSampledAt: Instant? = null,
    val elevation: List<ElevationPoint> = emptyList(),
    val driven: List<List<RoutePosition>> = emptyList(),
    val provider: String? = null, val durationSeconds: Long? = null,
)

data class FuelHorizon(
    val distanceM: Double, val liters: Double, val averageKml: Double?,
    val ascentM: Double?, val descentM: Double?, val terrain: String?, val fuelAtEndLiters: Double?,
)
data class SmartRange(val reachM: Double, val reserveLiters: Double, val deficitLiters: Double)
data class FuelComparison(
    val originalAt: Instant?, val comparisonStartedAt: Instant,
    val plannedToPositionLiters: Double?, val observedLiters: Double?, val differenceLiters: Double?,
    val projectedTotalLiters: Double?, val projectedDifferenceLiters: Double?,
    val differenceCents: Long?, val projectedDifferenceCents: Long?,
)
data class RouteFuelForecast(
    val routeId: String, val modelVersion: String, val confidence: String, val historySamples: Int, val computedAt: Instant,
    val next50: FuelHorizon?, val next100: FuelHorizon?, val destination: FuelHorizon?,
    val range: SmartRange?, val comparison: FuelComparison?,
) {
    fun withFinancialAccess(allowed: Boolean): RouteFuelForecast =
        if (allowed || comparison == null) this
        else copy(comparison = comparison.copy(differenceCents = null, projectedDifferenceCents = null))
}

/** Local observations are deltas of the existing integrated PID 183 trip counters. */
internal class LocalFuelHistory {
    private var previous: LocalFuelWindow? = null
    private var gap = false
    private val rows = ArrayDeque<FuelInterval>()

    fun clear() {
        previous = null
        gap = false
        rows.clear()
    }

    fun observe(window: LocalFuelWindow?): FuelHistory? {
        val current = window?.takeIf { it.trip?.distanceKm != null && it.trip.coveredMs != null && it.bootId != null && it.uptimeMs != null }
        val prior = previous
        if (current == null) {
            previous = null
            gap = rows.isNotEmpty()
            return null
        }
        previous = current
        if (prior == null) return null
        val before = requireNotNull(prior.trip)
        val after = requireNotNull(current.trip)
        val start = requireNotNull(before.distanceKm)
        val end = requireNotNull(after.distanceKm)
        val uptime = requireNotNull(current.uptimeMs)
        val elapsed = uptime - requireNotNull(prior.uptimeMs)
        val covered = requireNotNull(after.coveredMs) - requireNotNull(before.coveredMs)
        val liters = after.litres - before.litres
        if (prior.coreUid != current.coreUid || prior.bootId != current.bootId || end < start || covered < 0 || liters < 0 || elapsed < 0) {
            rows.clear()
            gap = false
            return FuelHistory(end, current.endedAt, "PID 183 local · distância pelo hodômetro", emptyList(), current.coreUid)
        }
        if (elapsed in 1..25_000 && covered > 0 && kotlin.math.abs(elapsed - covered) <= 2_000 &&
            end - start <= 1.5 && liters <= elapsed / 3_600.0) {
            rows.addLast(FuelInterval(start, end, end - start, liters, gapBefore = gap))
            if (rows.size > 4_320) rows.removeFirst()
            gap = false
        } else if (elapsed > 0) {
            gap = true
        }
        if (rows.isEmpty()) return null
        // Uma consulta repetida ou sem cobertura não torna o histórico mais recente.
        if (gap || elapsed == 0L) return null
        val sampledAt = current.slot?.let { current.endedAt.plusMillis(uptime - it * 10_000L) }
            ?: current.endedAt
        return FuelHistory(end, sampledAt, "PID 183 local · distância pelo hodômetro", rows.toList(), current.coreUid)
    }
}

/** Used when the Core reports full fuel windows and valid speed, with no odometer interval. */
internal class WindowFuelHistory {
    private var uid: String? = null
    private var boot: Long? = null
    private var lastSlot: Long? = null
    private var hadGap = false
    private var distanceKm = 0.0
    private var speedSource: String? = null
    private val speed = ArrayDeque<TimeSample>()
    private val rows = ArrayDeque<FuelInterval>()

    fun clear() {
        uid = null
        boot = null
        lastSlot = null
        hadGap = false
        distanceKm = 0.0
        speedSource = null
        speed.clear()
        rows.clear()
    }

    fun speedReading(coreUid: String?, reading: Reading?, now: Instant) {
        if (coreUid == null || reading?.source != Origin.CORE || reading.unit != "km/h") return
        val id = reading.captureId ?: return
        val at = Instant.ofEpochMilli(reading.sampleUptimeMs?.takeIf { it >= 0 } ?: return)
        val source = reading.sourceId ?: return
        val newBoot = reading.bootId ?: return
        if (coreUid != uid || newBoot != boot) {
            clear()
            uid = coreUid
            boot = newBoot
        }
        if (reading.quality == Quality.STALE || reading.sampledAt == null || reading.sampledAt.isAfter(now) ||
            reading.sampledAt.isBefore(now.minusSeconds(5))) return
        if (source != speedSource) {
            speed.clear()
            speedSource = source
        }
        if (speed.lastOrNull()?.at?.isAfter(at) == true) return
        val key = "$source/$id"
        // A janela de combustível e o PID 84 usam o mesmo relógio do Core.
        val sample = reading.value?.takeIf { it in 0.0..180.0 }
        if (speed.lastOrNull()?.id != key) speed.addLast(TimeSample(key, at, sample, reading.quality, source))
        while (speed.size > 90) speed.removeFirst()
    }

    fun observe(window: LocalFuelWindow?): FuelHistory? {
        if (window == null || window.coreUid != uid || window.bootId != boot) return null
        val slot = window.slot ?: return null
        val prior = lastSlot
        if (prior != null && slot <= prior) return null
        lastSlot = slot
        val endAtCore = Instant.ofEpochMilli(slot * 10_000L)
        val measured = timeStats(speed.toList(), endAtCore.minusSeconds(10), endAtCore,
            maxGapSeconds = 3, minimumCoverage = 1.0)
        if (measured == null || window.coveredMs != 10_000L) {
            hadGap = true
            return null
        }
        val end = distanceKm + measured.mean * 10.0 / 3600.0
        rows.addLast(FuelInterval(distanceKm, end, end - distanceKm, window.microlitres / 1_000_000.0,
            gapBefore = hadGap || prior != null && slot != prior + 1))
        if (rows.size > 4_320) rows.removeFirst()
        distanceKm = end
        hadGap = false
        return FuelHistory(distanceKm, window.endedAt, "PID 183 local · distância pela velocidade observada", rows.toList(), window.coreUid)
    }
}

internal fun dataAge(sampledAt: Instant, now: Instant): String {
    val seconds = Duration.between(sampledAt, now).seconds.coerceAtLeast(0)
    return when {
        seconds < 60 -> "${seconds}s atrás"
        seconds < 3_600 -> "${seconds / 60} min atrás"
        else -> "${seconds / 3_600} h atrás"
    }
}
