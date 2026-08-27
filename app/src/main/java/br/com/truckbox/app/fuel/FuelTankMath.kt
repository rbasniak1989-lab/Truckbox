package br.com.truckbox.app.fuel

import kotlin.math.max

/** Funções puras para cálculo do tanque virtual e autonomia. */
object FuelTankMath {
    fun remainingLiters(
        capacityLiters: Double,
        referenceRemainingLiters: Double,
        referenceFuelCounterLiters: Double,
        currentFuelCounterLiters: Double,
    ): Double? {
        if (capacityLiters <= 0.0 || !capacityLiters.isFinite()) return null
        if (!referenceRemainingLiters.isFinite() || !referenceFuelCounterLiters.isFinite() || !currentFuelCounterLiters.isFinite()) return null
        val consumed = max(0.0, currentFuelCounterLiters - referenceFuelCounterLiters)
        return (referenceRemainingLiters - consumed).coerceIn(0.0, capacityLiters)
    }

    /** Autonomia solicitada: litros disponíveis × média dos últimos 100 km. */
    fun rangeKm(remainingLiters: Double?, last100KmAverageKml: Double?): Double? {
        val liters = remainingLiters ?: return null
        val average = last100KmAverageKml ?: return null
        if (liters < 0.0 || average <= 0.0 || !average.isFinite()) return null
        return liters * average
    }
}
