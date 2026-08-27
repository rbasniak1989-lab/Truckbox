package br.com.truckbox.app.data

import br.com.truckbox.app.domain.health.HealthState

enum class DataQuality { VALID, UNAVAILABLE, STALE, SUSPECT, ALIAS }

data class SensorValue(
    val value: Double? = null,
    val quality: DataQuality = DataQuality.UNAVAILABLE,
    val receivedAtMs: Long = 0L,
    val source: String? = null,
) {
    fun usable(): Boolean = value != null && quality == DataQuality.VALID
    fun markStale(nowMs: Long, timeoutMs: Long): SensorValue =
        if (quality == DataQuality.VALID && receivedAtMs > 0L && nowMs - receivedAtMs > timeoutMs) copy(quality = DataQuality.STALE) else this
}

enum class OperationMode { NO_TRIP, LOADED_TRIP, EMPTY_MODE }

data class ConnectionState(
    val wifiBound: Boolean = false,
    val tcpConnected: Boolean = false,
    val canActive: Boolean = false,
    val framesPerSecond: Double = 0.0,
    val totalFrames: Long = 0L,
    val invalidLines: Long = 0L,
    val reconnects: Long = 0L,
    val lastFrameAgeMs: Long? = null,
    val lastError: String? = null,
    val sessionSeconds: Long = 0L,
)

data class GpsState(
    val hasFix: Boolean = false,
    val latitude: Double? = null,
    val longitude: Double? = null,
    val speedKmh: Double? = null,
    val altitudeM: Double? = null,
    val accuracyM: Double? = null,
    val satellites: Int = 0,
    val receivedAtMs: Long = 0L,
)

data class EngineState(
    val rpm: SensorValue = SensorValue(),
    val acceleratorPct: SensorValue = SensorValue(),
    val actualTorquePct: SensorValue = SensorValue(),
    val powerKw: SensorValue = SensorValue(),
    val coolantTempC: SensorValue = SensorValue(),
    val oilTempC: SensorValue = SensorValue(),
    val intakeAirTempC: SensorValue = SensorValue(),
    val oilPressureBar: SensorValue = SensorValue(),
    val oilLevelPct: SensorValue = SensorValue(),
    val fuelPressureBar: SensorValue = SensorValue(),
    val boostBar: SensorValue = SensorValue(),
    val ambientTempC: SensorValue = SensorValue(),
)

data class TransmissionState(
    val currentGear: Int? = null,
    val selectedGear: Int? = null,
    val actualGearRatio: Double? = null,
    val inputShaftRpm: SensorValue = SensorValue(),
    val outputShaftRpm: SensorValue = SensorValue(),
    val clutchSlipPct: SensorValue = SensorValue(),
    val oilTempC: SensorValue = SensorValue(),
    val iRollActive: Boolean = false,
    val cruiseActive: Boolean = false,
    val vebStageRaw: Int = 0,
    val ishiftMode: String? = null,
    val prediction: IShiftPredictionUi = IShiftPredictionUi(),
)

data class IShiftPredictionUi(
    val phase: String = "NONE",
    val predictedGear: Int? = null,
    val confirmedGear: Int? = null,
    /** Score operacional de exibição; não é probabilidade estatística calibrada. */
    val score: Double = 0.0,
    val projectedRpm: Double? = null,
    // Diagnóstico de campo do I-Shift Predict V2 (visível somente no Logger).
    val riskScore: Double = 0.0,
    val rpmSlopePerSecond: Double = 0.0,
    val speedSlopeKmhPerSecond: Double = 0.0,
    val stableMs: Long = 0L,
    val estimatedSecondsToLowRpm: Double? = null,
)

data class WeightState(
    val driveBogieKg: SensorValue = SensorValue(),
    val combinationKg: SensorValue = SensorValue(),
)

data class FuelState(
    val fuelRateLph: SensorValue = SensorValue(),
    val fuelReady: Boolean = false,
    val distanceReady: Boolean = false,
    val sessionLiters: Double = 0.0,
    val sessionDistanceKm: Double = 0.0,
    val sessionAverageKml: Double? = null,
    val last50KmAverageKml: Double? = null,
    val best50KmAverageKml: Double? = null,
    /** Janela móvel aprovada para a aba Consumo. */
    val last100KmAverageKml: Double? = null,
)

data class OperationState(
    val mode: OperationMode = OperationMode.NO_TRIP,
    val activeTripId: String? = null,
)

data class DrivingAnalyticsState(
    val movingSeconds: Long = 0L,
    val engineOnSeconds: Long = 0L,
    val iRollSeconds: Long = 0L,
    val cruiseSeconds: Long = 0L,
    val idleSeconds: Long = 0L,
    val highPedalSeconds: Long = 0L,
    // Critérios adicionais do Eco Score v2. São métricas de campo provisórias,
    // não uma certificação de eficiência do fabricante.
    val efficientRpmSeconds: Long = 0L,
    val economicSpeedSeconds: Long = 0L,
    val justifiedHighDemandSeconds: Long = 0L,
    val smartTransmissionSeconds: Long = 0L,
    val vebSeconds: Long = 0L,
) {
    val iRollPercent: Double? get() = if (movingSeconds > 0) iRollSeconds * 100.0 / movingSeconds else null
    val cruisePercent: Double? get() = if (movingSeconds > 0) cruiseSeconds * 100.0 / movingSeconds else null
    val idlePercent: Double? get() = if (engineOnSeconds > 0) idleSeconds * 100.0 / engineOnSeconds else null
    val highPedalPercent: Double? get() = if (movingSeconds > 0) highPedalSeconds * 100.0 / movingSeconds else null
    val efficientRpmPercent: Double? get() = if (movingSeconds > 0) efficientRpmSeconds * 100.0 / movingSeconds else null
    val economicSpeedPercent: Double? get() = if (movingSeconds > 0) economicSpeedSeconds * 100.0 / movingSeconds else null
    val unjustifiedHighPedalPercent: Double? get() = if (movingSeconds > 0)
        (highPedalSeconds - justifiedHighDemandSeconds).coerceAtLeast(0L) * 100.0 / movingSeconds else null
    val smartTransmissionPercent: Double? get() = if (movingSeconds > 0) smartTransmissionSeconds * 100.0 / movingSeconds else null
    val vebPercent: Double? get() = if (movingSeconds > 0) vebSeconds * 100.0 / movingSeconds else null
}

data class RawFrameSummary(
    val timestampMs: Long,
    val pgn: Int,
    val sa: Int,
    val canIdHex: String,
    val dataHex: String,
)

data class LoggerState(
    val pgnCounts: Map<Int, Long> = emptyMap(),
    val recentFrames: List<RawFrameSummary> = emptyList(),
    val lastSavedFile: String? = null,
    val ringBufferFrames: Int = 0,
)

data class TruckState(
    val speedKmh: SensorValue = SensorValue(),
    val odometerKm: SensorValue = SensorValue(),
    val connection: ConnectionState = ConnectionState(),
    val gps: GpsState = GpsState(),
    val engine: EngineState = EngineState(),
    val transmission: TransmissionState = TransmissionState(),
    val weights: WeightState = WeightState(),
    val fuel: FuelState = FuelState(),
    val operation: OperationState = OperationState(),
    val analytics: DrivingAnalyticsState = DrivingAnalyticsState(),
    val health: HealthState = HealthState(),
    val logger: LoggerState = LoggerState(),
)
