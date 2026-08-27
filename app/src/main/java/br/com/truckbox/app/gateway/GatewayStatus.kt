package br.com.truckbox.app.gateway

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

data class GatewaySnapshot(
    val running: Boolean = false,
    val coreOnline: Boolean = false,
    val cloudOnline: Boolean = false,
    val gpsFix: Boolean = false,
    val pendingBytes: Long = 0L,
    val lastAckSeq: Long = 0L,
    val lastBatchCount: Int = 0,
    val lastUploadMs: Long? = null,
    val lastGpsUploadMs: Long? = null,
    val odometerKm: Double? = null,
    val coreFuelLiters: Double? = null,
    val speedKmh: Double? = null,
    val rpm: Double? = null,
    val combinationWeightKg: Double? = null,
    val lastError: String? = null,
)

object GatewayStatus {
    private val _state = MutableStateFlow(GatewaySnapshot())
    val state = _state.asStateFlow()
    fun update(block: (GatewaySnapshot) -> GatewaySnapshot) { _state.value = block(_state.value) }
}
