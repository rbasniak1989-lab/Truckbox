package br.com.truckbox.app.domain.efficiency

import br.com.truckbox.app.data.DrivingAnalyticsState
import br.com.truckbox.app.data.FuelState
import kotlin.math.roundToInt

data class EcoCriterion(
    val key: String,
    val label: String,
    val score: Int?,
    val weight: Double,
)

data class EcoBreakdown(
    val score: Int?,
    val criteria: List<EcoCriterion>,
)

/**
 * Eco Score v2 de campo.
 *
 * Pesos aprovados para o TruckBox:
 * - eficiência real do combustível: 30%
 * - antecipação / embalo: 15%
 * - faixa eficiente de RPM: 15%
 * - marcha lenta: 10%
 * - torque/pedal sob carga: 10%
 * - velocidade econômica: 10%
 * - uso inteligente da transmissão: 10%
 *
 * As faixas/limiares ainda são provisórios e serão calibrados com logs reais.
 */
object EcoEfficiency {
    fun calculate(fuel: FuelState, analytics: DrivingAnalyticsState): EcoBreakdown {
        val enoughDriving = analytics.movingSeconds >= 60L
        val fuelScore = fuelEfficiencyScore(fuel)
        val idleScore = if (analytics.engineOnSeconds >= 60L) {
            val idle = analytics.idlePercent ?: 0.0
            (100.0 - idle * 4.0).roundToInt().coerceIn(0, 100)
        } else null
        val rpmScore = if (enoughDriving) targetScore(analytics.efficientRpmPercent, targetPct = 80.0) else null
        val speedScore = if (enoughDriving) targetScore(analytics.economicSpeedPercent, targetPct = 75.0) else null
        val torquePedalScore = if (enoughDriving) {
            val unnecessary = analytics.unjustifiedHighPedalPercent ?: 0.0
            (100.0 - unnecessary * 3.0).roundToInt().coerceIn(0, 100)
        } else null
        val transmissionScore = if (enoughDriving) targetScore(analytics.smartTransmissionPercent, targetPct = 82.0) else null
        val anticipationScore = if (enoughDriving) {
            val iRoll = analytics.iRollPercent ?: 0.0
            val veb = analytics.vebPercent ?: 0.0
            // Não recompensa simplesmente "mais freio"; usa VEB apenas como evidência
            // de antecipação/controle e limita sua contribuição.
            (65.0 + iRoll.coerceAtMost(20.0) * 1.35 + veb.coerceAtMost(16.0) * 0.50)
                .roundToInt().coerceIn(0, 100)
        } else null

        val criteria = listOf(
            EcoCriterion("fuel", "Eficiência combustível", fuelScore, 0.30),
            EcoCriterion("anticipation", "Antecipação / I-Roll", anticipationScore, 0.15),
            EcoCriterion("rpm", "Faixa eficiente de RPM", rpmScore, 0.15),
            EcoCriterion("idle", "Marcha lenta", idleScore, 0.10),
            EcoCriterion("torque", "Torque / pedal sob carga", torquePedalScore, 0.10),
            EcoCriterion("speed", "Velocidade econômica", speedScore, 0.10),
            EcoCriterion("transmission", "Uso inteligente da transmissão", transmissionScore, 0.10),
        )

        val available = criteria.filter { it.score != null }
        val weight = available.sumOf { it.weight }
        val overall = if (weight >= 0.50) {
            (available.sumOf { (it.score ?: 0) * it.weight } / weight).roundToInt().coerceIn(0, 100)
        } else null

        return EcoBreakdown(overall, criteria)
    }

    private fun fuelEfficiencyScore(fuel: FuelState): Int? {
        val trip = fuel.sessionAverageKml ?: return null
        val last100 = fuel.last100KmAverageKml ?: return null
        if (trip <= 0.0 || last100 <= 0.0) return null
        val ratio = last100 / trip
        return (70.0 + ((ratio - 0.90) / 0.15) * 30.0).roundToInt().coerceIn(0, 100)
    }

    private fun targetScore(valuePct: Double?, targetPct: Double): Int? {
        val value = valuePct ?: return null
        return (value / targetPct * 100.0).roundToInt().coerceIn(0, 100)
    }
}
