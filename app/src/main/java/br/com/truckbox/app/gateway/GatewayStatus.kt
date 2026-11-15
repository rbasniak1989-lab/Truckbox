package br.com.truckbox.app.gateway

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

data class GatewaySnapshot(
    val running: Boolean = false,
    val gpsCloudOnline: Boolean = false,
    val operationsCloudOnline: Boolean = false,
    val gpsFix: Boolean = false,
    val pendingGps: Int = 0,
    val pendingOperations: Int = 0,
    val gpsEnabled: Boolean = true,
    val lastGpsCaptureMs: Long? = null,
    val gpsAccuracyM: Double? = null,
    val operationsError: String? = null,
    val lastUploadMs: Long? = null,
    val lastGpsUploadMs: Long? = null,
    val lastGpsSentCaptureMs: Long? = null,
    val lastError: String? = null,
) {val cloudOnline: Boolean get()=gpsCloudOnline||operationsCloudOnline}

object GatewayStatus {
    private val _state = MutableStateFlow(GatewaySnapshot())
    val state = _state.asStateFlow()
    fun update(block: (GatewaySnapshot) -> GatewaySnapshot) {synchronized(this) {_state.value=block(_state.value)}}
}
