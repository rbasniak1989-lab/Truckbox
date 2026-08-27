package br.com.truckbox.app.domain.transmission

enum class IShiftPredictionPhase { NONE, PREDICTED, CONFIRMED }

data class IShiftPredictionState(
    val phase: IShiftPredictionPhase = IShiftPredictionPhase.NONE,
    val currentGear: Int? = null,
    val predictedGear: Int? = null,
    val confirmedGear: Int? = null,
    /** Score operacional de exibição; não é probabilidade estatisticamente calibrada. */
    val confidence: Double = 0.0,
    val projectedRpm: Double? = null,
    val riskScore: Double = 0.0,
    val rpmSlopePerSecond: Double = 0.0,
    val speedSlopeKmhPerSecond: Double = 0.0,
    val stableMs: Long = 0,
    /** Usado somente para diagnóstico/calibração; não exibir countdown ao motorista. */
    val estimatedSecondsToLowRpm: Double? = null,
)
