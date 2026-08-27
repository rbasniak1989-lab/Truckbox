package br.com.truckbox.app.domain.health

enum class HealthSeverity { OK, INFO, LEARNING, WARNING, CRITICAL, UNAVAILABLE }
enum class HealthConfidence { LOW, MEDIUM, HIGH }
enum class HealthGroup { ENGINE, TRANSMISSION, DRIVETRAIN, FUEL, COOLING, INTAKE }

data class HealthItem(
    val id: String,
    val group: HealthGroup,
    val title: String,
    val severity: HealthSeverity,
    val headline: String,
    val detail: String,
    val score: Int? = null,
    val confidence: HealthConfidence = HealthConfidence.LOW,
    val basedOn: List<String> = emptyList(),
)

data class HealthLearningState(
    val observedSeconds: Long = 0L,
    val oilPressureRatioBaseline: Double? = null,
    val oilPressureBaselineSamples: Long = 0L,
    val boostPerTorqueBaseline: Double? = null,
    val boostBaselineSamples: Long = 0L,
    val fuelPressureBaselineBar: Double? = null,
    val fuelPressureBaselineSamples: Long = 0L,
    val shiftCount: Long = 0L,
    val slowShiftCount: Long = 0L,
    val shiftDurationEwmaSec: Double? = null,
    val shiftPeakSlipEwmaPct: Double? = null,
    val huntingEvents: Long = 0L,
    val engineStressIntegral: Double = 0.0,
    val engineSevereSeconds: Long = 0L,
    val transmissionStressIntegral: Double = 0.0,
    val drivetrainStressIntegral: Double = 0.0,
)

data class HealthState(
    val items: List<HealthItem> = emptyList(),
    val engineCurrentStress: Int = 0,
    val engineAverageStress: Int = 0,
    val transmissionCurrentStress: Int = 0,
    val transmissionAverageStress: Int = 0,
    val drivetrainCurrentStress: Int = 0,
    val drivetrainAverageStress: Int = 0,
    val learning: HealthLearningState = HealthLearningState(),
    val updatedAtMs: Long = 0L,
) {
    fun item(id: String): HealthItem? = items.firstOrNull { it.id == id }
}
