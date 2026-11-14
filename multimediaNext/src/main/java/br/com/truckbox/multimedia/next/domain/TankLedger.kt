package br.com.truckbox.multimedia.next.domain

import java.time.Instant

enum class TankEventKind { BOTH_FULL, PARTIAL, COUNTER, GAP }
data class TankEvent(
    val id: String, val vehicleId: String, val at: Instant, val kind: TankEventKind,
    val counterLiters: Double? = null, val counterEpoch: String? = null,
    val addedLiters: Double? = null, val bothTanksConfirmed: Boolean = false,
)
enum class TankQuality { UNCALIBRATED, VALID, STALE, CONFLICT }
data class TankBalance(val remainingLiters: Double?, val quality: TankQuality, val countedEvents: Int)

/** Referência de reconciliação para o contrato backend, não substitui ledger Cloud.
 * Reconstrói todo o conjunto por tempo; ID estável deduplica replay. Boia não é entrada. */
fun reconcileTank(capacity: Double, vehicleId: String, events: List<TankEvent>): TankBalance {
    if (!capacity.isFinite() || capacity <= 0) return TankBalance(null,TankQuality.CONFLICT,0)
    val grouped = events.filter { it.vehicleId == vehicleId }.groupBy { it.id }
    if (grouped.any { (id, values) -> id.isBlank() || values.distinct().size > 1 })
        return TankBalance(null,TankQuality.CONFLICT,grouped.size)
    val ordered = grouped.values.map { it.first() }.sortedWith(compareBy<TankEvent> { it.at }.thenBy { it.id })
    // Sem ordem causal entre dois eventos de combustível simultâneos, não adivinhar.
    if (ordered.groupBy { it.at }.values.any { it.size > 1 }) return TankBalance(null,TankQuality.CONFLICT,ordered.size)
    var balance: Double? = null
    var previous: Double? = null
    var epoch: String? = null
    var quality = TankQuality.UNCALIBRATED
    for (event in ordered) {
        when (event.kind) {
            TankEventKind.BOTH_FULL -> {
                if (!event.bothTanksConfirmed || event.counterLiters?.let { !it.isFinite() || it < 0 } != false || event.counterEpoch.isNullOrBlank()) {
                    quality = TankQuality.CONFLICT; balance = null; continue
                }
                balance = capacity; previous = event.counterLiters; epoch = event.counterEpoch; quality = TankQuality.VALID
            }
            TankEventKind.PARTIAL -> {
                val added = event.addedLiters
                if (added == null || !added.isFinite() || added <= 0 || added > capacity) {
                    balance = null; quality = TankQuality.CONFLICT
                } else if (balance != null && quality == TankQuality.VALID) balance = minOf(capacity,balance+added)
            }
            TankEventKind.COUNTER -> {
                val counter = event.counterLiters
                if (balance == null || quality != TankQuality.VALID) continue
                val before = previous
                if (counter == null || !counter.isFinite() || before == null || counter < before || event.counterEpoch != epoch) {
                    quality = TankQuality.STALE; continue
                }
                val next = balance-(counter-before)
                if (next < 0) { balance = null; quality = TankQuality.CONFLICT } else balance = next
                previous = counter
            }
            TankEventKind.GAP -> if (balance != null) quality = TankQuality.STALE
        }
    }
    return TankBalance(balance,quality,ordered.size)
}
