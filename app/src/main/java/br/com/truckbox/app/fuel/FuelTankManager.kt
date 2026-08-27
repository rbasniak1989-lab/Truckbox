package br.com.truckbox.app.fuel

import android.content.Context

/**
 * Tanque virtual do TruckBox.
 *
 * O nível estimado NÃO depende do sensor OEM de nível. Ele é reconciliado em
 * abastecimentos e desconta o combustível calculado pelo TruckBox.
 *
 * Regra:
 *   nível atual = nível de referência - (contador combustível atual - contador de referência)
 *
 * Um abastecimento marcado como tanque cheio calibra o nível em 100% da
 * capacidade configurada. Um abastecimento parcial soma os litros informados.
 */
class FuelTankManager(context: Context) {
    private val p = context.getSharedPreferences("truckbox_fuel_tank_v1", Context.MODE_PRIVATE)

    var capacityLiters: Double
        get() = p.getDoubleCompat("capacity_l") ?: 0.0
        private set(v) = p.edit().putDoubleCompat("capacity_l", v).apply()

    private var referenceRemainingLiters: Double?
        get() = p.getDoubleCompat("reference_remaining_l")
        set(v) = p.edit().putDoubleCompat("reference_remaining_l", v).apply()

    private var referenceFuelCounterLiters: Double?
        get() = p.getDoubleCompat("reference_fuel_counter_l")
        set(v) = p.edit().putDoubleCompat("reference_fuel_counter_l", v).apply()

    private var lastObservedFuelCounterLiters: Double?
        get() = p.getDoubleCompat("last_observed_fuel_counter_l")
        set(v) = p.edit().putDoubleCompat("last_observed_fuel_counter_l", v).apply()

    fun setCapacity(liters: Double, currentFuelCounterLiters: Double) {
        val newCapacity = liters.coerceAtLeast(0.0)
        val current = estimateRemaining(currentFuelCounterLiters)
        capacityLiters = newCapacity
        if (current != null) {
            setReference(current.coerceIn(0.0, newCapacity), currentFuelCounterLiters)
        }
    }

    /**
     * Chamar quando o contador acumulado do TruckBox mudar.
     * Se o contador for zerado/reiniciado, preserva o nível estimado e cria uma
     * nova referência para não "devolver" combustível ao tanque.
     */
    fun syncFuelCounter(currentFuelCounterLiters: Double) {
        if (!currentFuelCounterLiters.isFinite()) return
        val previous = lastObservedFuelCounterLiters
        if (previous != null && currentFuelCounterLiters + 0.05 < previous) {
            val currentRemaining = estimateRemaining(previous)
            if (currentRemaining != null) {
                setReference(currentRemaining, currentFuelCounterLiters)
            }
        }
        lastObservedFuelCounterLiters = currentFuelCounterLiters
    }

    fun registerFueling(
        litersAdded: Double,
        fullTank: Boolean,
        currentFuelCounterLiters: Double,
    ) {
        val cap = capacityLiters
        if (cap <= 0.0) return

        val afterFueling = if (fullTank) {
            cap
        } else {
            val before = estimateRemaining(currentFuelCounterLiters) ?: return
            (before + litersAdded.coerceAtLeast(0.0)).coerceAtMost(cap)
        }
        setReference(afterFueling, currentFuelCounterLiters)
    }

    /** Permite calibração inicial manual, útil antes do primeiro tanque cheio. */
    fun setEstimatedRemaining(liters: Double, currentFuelCounterLiters: Double) {
        val cap = capacityLiters
        if (cap <= 0.0) return
        setReference(liters.coerceIn(0.0, cap), currentFuelCounterLiters)
    }

    /** Aplica a referência autoritativa criada pela dashboard/Cloud. */
    fun applyCloudState(
        capacity: Double?,
        estimatedAtReference: Double?,
        referenceFuelCounter: Double?,
        currentFuelCounterLiters: Double,
    ) {
        if (capacity != null && capacity.isFinite() && capacity > 0.0) {
            capacityLiters = capacity
        }
        val cap = capacityLiters
        if (cap > 0.0 && estimatedAtReference != null && estimatedAtReference.isFinite()) {
            setReference(
                estimatedAtReference.coerceIn(0.0, cap),
                referenceFuelCounter?.takeIf { it.isFinite() } ?: currentFuelCounterLiters,
            )
        }
        syncFuelCounter(currentFuelCounterLiters)
    }

    fun estimateRemaining(currentFuelCounterLiters: Double): Double? {
        val cap = capacityLiters
        val refRemaining = referenceRemainingLiters ?: return null
        val refCounter = referenceFuelCounterLiters ?: return null
        if (cap <= 0.0 || !currentFuelCounterLiters.isFinite()) return null

        return FuelTankMath.remainingLiters(
            capacityLiters = cap,
            referenceRemainingLiters = refRemaining,
            referenceFuelCounterLiters = refCounter,
            currentFuelCounterLiters = currentFuelCounterLiters,
        )
    }

    fun percent(currentFuelCounterLiters: Double): Double? {
        val cap = capacityLiters
        val remaining = estimateRemaining(currentFuelCounterLiters) ?: return null
        return if (cap > 0.0) remaining * 100.0 / cap else null
    }

    fun rangeKm(currentFuelCounterLiters: Double, last100KmAverageKml: Double?): Double? {
        return FuelTankMath.rangeKm(estimateRemaining(currentFuelCounterLiters), last100KmAverageKml)
    }

    private fun setReference(remaining: Double, fuelCounter: Double) {
        referenceRemainingLiters = remaining
        referenceFuelCounterLiters = fuelCounter
        lastObservedFuelCounterLiters = fuelCounter
    }
}

private fun android.content.SharedPreferences.getDoubleCompat(key: String): Double? =
    if (!contains(key)) null else Double.fromBits(getLong(key, 0L))

private fun android.content.SharedPreferences.Editor.putDoubleCompat(
    key: String,
    value: Double?,
): android.content.SharedPreferences.Editor {
    if (value == null) remove(key) else putLong(key, value.toRawBits())
    return this
}
