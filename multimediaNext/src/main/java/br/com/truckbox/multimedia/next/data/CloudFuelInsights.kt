package br.com.truckbox.multimedia.next.data

import br.com.truckbox.multimedia.next.domain.*
import java.time.Instant
import java.time.OffsetDateTime

data class FuelRangeDto(
    val capacityLiters: Double? = null, val remainingLiters: Double? = null, val percent: Double? = null,
    val fullTankAt: String? = null, val fullTankOdometerKm: Double? = null,
    val consumedSinceFullLiters: Double? = null, val partialAddedLiters: Double? = null,
    val averageKml: Double? = null, val averageSource: String? = null, val autonomyKm: Double? = null,
    val quality: String? = null, val updatedAt: String? = null,
) {
    fun domain(received: Instant): FuelRange {
        val capacity = capacityLiters.positive()
        val balance = remainingLiters.nonnegative()?.takeIf { capacity == null || it <= capacity }
        return FuelRange(capacity, balance, percent?.takeIf { it in 0.0..100.0 && balance != null },
            fullTankAt.wireTime()?.takeUnless { it.isAfter(received.plusSeconds(5)) }, fullTankOdometerKm.nonnegative(),
            consumedSinceFullLiters.nonnegative(), partialAddedLiters.nonnegative(), averageKml.positive(),
            averageSource?.takeIf { it.matches(code) }, autonomyKm.nonnegative()?.takeIf { balance != null },
            quality?.takeIf { it.matches(code) } ?: "UNKNOWN",
            updatedAt.wireTime()?.takeUnless { it.isAfter(received.plusSeconds(5)) } ?: received)
    }
}

data class FuelIntervalDto(
    val fromKm: Double? = null, val toKm: Double? = null, val coveredKm: Double? = null,
    val liters: Double? = null, val idleLiters: Double? = null, val plannedLiters: Double? = null, val altitudeM: Double? = null,
) {
    fun domain(terrainExpected: Boolean): FuelInterval {
        val start = requireNotNull(fromKm.nonnegative())
        val end = requireNotNull(toKm.nonnegative())
        val covered = requireNotNull(coveredKm.nonnegative())
        val used = requireNotNull(liters.nonnegative())
        require(end >= start && covered <= end - start + 0.001)
        return FuelInterval(start, end, covered, used, idleLiters.nonnegative()?.takeIf { it <= used },
            plannedLiters.nonnegative().takeIf { terrainExpected }, altitudeM?.takeIf { it in -500.0..9_000.0 })
    }
}

data class FuelHistoryDto(
    val tripId: String? = null, val throughKm: Double? = null, val sampledAt: String? = null,
    val source: String? = null, val predictionMethod: String? = null, val intervals: List<FuelIntervalDto>? = null,
) {
    fun domain(trip: String, received: Instant): FuelHistory {
        require(tripId == trip && source in setOf("PID250_RECONCILED", "PID183_INTEGRATED", "MIXED_DISJOINT"))
        val end = requireNotNull(throughKm.nonnegative())
        val at = requireNotNull(sampledAt.wireTime())
        require(!at.isAfter(received.plusSeconds(5)))
        val rows = requireNotNull(intervals)
        require(rows.size <= 4_320)
        val parsed = rows.map { it.domain(predictionMethod == TERRAIN_MODEL) }
        require(parsed.all { it.toKm <= end + 0.001 })
        require(parsed.zipWithNext().all { (a, b) -> a.toKm <= b.fromKm })
        val label = when (source) {
            "PID250_RECONCILED" -> "Cloud · PID 250"
            "PID183_INTEGRATED" -> "Cloud · PID 183 integrado"
            else -> "Cloud · PID 250 e PID 183 em intervalos distintos"
        }
        return FuelHistory(end, at, label, parsed)
    }
}

data class ElevationPointDto(val distanceM: Double? = null, val altitudeM: Double? = null)
data class RouteTollDto(val name: String? = null)
data class JourneyRouteDto(
    val id: String? = null, val tripId: String? = null, val revision: Int? = null,
    val distanceM: Double? = null, val polyline: String? = null,
    val tolls: List<RouteTollDto>? = null, val validUntil: String? = null,
    val remainingM: Double? = null, val progressSampledAt: String? = null,
    val elevation: List<ElevationPointDto>? = null,
    val drivenPolylines: List<String>? = null,
) {
    fun domain(trip: String, received: Instant): JourneyRoute {
        require(tripId == trip && id?.let(UUID_PATTERN::matches) == true)
        val distance = requireNotNull(distanceM.positive())
        require(distance <= 25_000_000)
        val end = validUntil.wireTime()
        require(end == null || end.isAfter(received))
        val shape = decodeRoute(requireNotNull(polyline))
        var profile = elevation.orEmpty().takeIf { it.size in 2..1_200 }.orEmpty().mapNotNull {
            val at = it.distanceM?.takeIf { m -> m in 0.0..distance }
            val altitude = it.altitudeM?.takeIf { m -> m in -500.0..9_000.0 }
            if (at != null && altitude != null) ElevationPoint(at, altitude) else null
        }
        if (profile.size != elevation.orEmpty().size || profile.zipWithNext().any { (a, b) -> a.distanceM >= b.distanceM }) profile = emptyList()
        val progressAt = progressSampledAt.wireTime()?.takeUnless { it.isAfter(received.plusSeconds(5)) }
        return JourneyRoute(id, trip, revision?.takeIf { it > 0 }, distance, shape,
            tolls.orEmpty().take(100).mapNotNull { it.name?.takeIf { name -> name.length in 1..160 } }, end,
            remainingM?.takeIf { it in 0.0..distance && progressAt != null }, progressAt, profile,
            drivenPolylines.orEmpty().takeIf { it.size <= 32 && it.sumOf(String::length) <= 42_000 }
                .orEmpty().mapNotNull { runCatching { decodeRoute(it) }.getOrNull() })
    }
}

data class FuelHorizonDto(
    val distanceM: Double? = null, val liters: Double? = null, val averageKml: Double? = null,
    val ascentM: Double? = null, val descentM: Double? = null, val terrain: String? = null,
    val fuelAtEndLiters: Double? = null,
) {
    fun domain(limitM: Double): FuelHorizon {
        val distance = requireNotNull(distanceM?.takeIf { it in 0.0..limitM })
        val used = requireNotNull(liters.nonnegative())
        return FuelHorizon(distance, used, averageKml.positive(), ascentM.nonnegative(), descentM.nonnegative(),
            terrain?.takeIf { it in setOf("ASCENT", "DESCENT", "FLAT", "MIXED") }, fuelAtEndLiters?.takeIf { it.isFinite() })
    }
}
data class FuelHorizonsDto(
    val next50: FuelHorizonDto? = null, val next100: FuelHorizonDto? = null, val destination: FuelHorizonDto? = null,
)
data class SmartRangeDto(val reachM: Double? = null, val reserveLiters: Double? = null, val deficitLiters: Double? = null)
data class FuelComparisonDto(
    val originalAt: String? = null, val comparisonStartedAt: String? = null,
    val plannedToPositionLiters: Double? = null, val observedLiters: Double? = null, val differenceLiters: Double? = null,
    val projectedTotalLiters: Double? = null, val projectedDifferenceLiters: Double? = null,
    val differenceCents: Long? = null, val projectedDifferenceCents: Long? = null,
)
data class RouteFuelForecastDto(
    val method: String? = null, val tripId: String? = null, val routeId: String? = null, val routeRevision: Int? = null,
    val modelVersion: String? = null, val confidence: String? = null, val historySamples: Int? = null,
    val computedAt: String? = null, val horizons: FuelHorizonsDto? = null, val smartRange: SmartRangeDto? = null,
    val comparison: FuelComparisonDto? = null,
) {
    fun domain(route: JourneyRoute, received: Instant): RouteFuelForecast {
        require(method == TERRAIN_MODEL && tripId == route.tripId && routeId == route.id &&
            route.revision != null && routeRevision == route.revision)
        val evaluation = requireNotNull(computedAt.wireTime())
        require(!evaluation.isAfter(received.plusSeconds(5)))
        require(modelVersion != null && modelVersion.length in 1..64 && confidence in setOf("LOW", "MEDIUM", "HIGH") &&
            historySamples != null && historySamples >= 0)
        require(historySamples > 0 || confidence == "LOW")
        val remaining = requireNotNull(route.remainingM)
        val range = smartRange?.let {
            SmartRange(requireNotNull(it.reachM?.takeIf { meters -> meters in 0.0..remaining }),
                requireNotNull(it.reserveLiters.nonnegative()), requireNotNull(it.deficitLiters.nonnegative()))
        }
        return RouteFuelForecast(route.id, modelVersion, confidence!!, historySamples, evaluation,
            horizons?.next50?.domain(minOf(50_000.0, remaining)), horizons?.next100?.domain(minOf(100_000.0, remaining)),
            horizons?.destination?.domain(remaining), range, comparison?.let {
                FuelComparison(it.originalAt.wireTime(), requireNotNull(it.comparisonStartedAt.wireTime()),
                    it.plannedToPositionLiters.nonnegative(), it.observedLiters.nonnegative(), it.differenceLiters?.takeIf(Double::isFinite),
                    it.projectedTotalLiters.nonnegative(), it.projectedDifferenceLiters?.takeIf(Double::isFinite),
                    it.differenceCents, it.projectedDifferenceCents)
            })
    }
}

private const val TERRAIN_MODEL = "TRUCK_TERRAIN_HISTORY_V1"
private val code = Regex("[A-Z0-9_]{1,50}")
private fun Double?.nonnegative() = this?.takeIf { it.isFinite() && it >= 0 }
private fun Double?.positive() = this?.takeIf { it.isFinite() && it > 0 }
private fun String?.wireTime(): Instant? = this?.let { runCatching { OffsetDateTime.parse(it).toInstant() }.getOrNull() }

private fun decodeRoute(encoded: String): List<RoutePosition> {
    require(encoded.length in 4..42_000)
    var cursor = 0
    fun delta(): Long {
        var sum = 0L
        for (shift in 0..30 step 5) {
            require(cursor < encoded.length)
            val digit = encoded[cursor++].code - 63
            require(digit in 0..63)
            sum = sum or ((digit and 31).toLong() shl shift)
            if (digit < 32) return (sum shr 1) xor -(sum and 1)
        }
        throw IllegalArgumentException("Invalid polyline")
    }
    var lat = 0L
    var lon = 0L
    return buildList {
        while (cursor < encoded.length) {
            lat += delta()
            lon += delta()
            require(lat in -9_000_000L..9_000_000L && lon in -18_000_000L..18_000_000L)
            add(RoutePosition(lat / 100_000.0, lon / 100_000.0))
        }
    }.also { require(it.size >= 2) }
}
