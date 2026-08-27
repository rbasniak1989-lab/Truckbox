package br.com.truckbox.app.domain.transmission

import kotlin.math.abs
import kotlin.math.pow

/**
 * TruckBox I-Shift Predict V2 de campo.
 *
 * Duas camadas claramente separadas:
 * 1) PREDICTED: previsão TruckBox antes da decisão da TCM.
 * 2) CONFIRMED: Selected Gear != Current Gear; confirmação direta da I-Shift.
 *
 * V2 corrige dois problemas observados na primeira build de campo:
 * - o bootstrap de relações só conhecia 6ª/7ª/8ª, deixando 9ª–12ª sem candidato
 *   durante boa parte da viagem;
 * - o limiar do score V1 era conservador demais e normalmente só liberava o aviso
 *   quando a Selected Gear já estava mudando.
 *
 * O destino continua usando a regra física validada no estudo do Log Mãe:
 * RPM pós-troca ~= RPM atual * relação candidata / relação atual,
 * escolhendo a marcha inferior que leva a RPM para perto de ~1200 rpm.
 *
 * O gatilho temporal V2 é deliberadamente interpretável: sob carga, estima quanto
 * tempo falta para a RPM atual atingir a faixa em que as reduções do Log Mãe
 * ocorreram (~1025 rpm), combinando isso com queda de RPM/velocidade e carga.
 */
class IShiftPredictor(
    seedRatios: Map<Int, Double> = mapOf(
        6 to 4.348,
        7 to 3.436,
        8 to 2.698,
        9 to 2.078,
    ),
) {
    data class Input(
        val timeMs: Long,
        val currentGear: Int?,
        val selectedGear: Int?,
        val actualGearRatio: Double?,
        val rpm: Double?,
        val speedKmh: Double?,
        val torquePct: Double?,
        val pedalPct: Double?,
        val powerKw: Double? = null,
        val clutchSlipPct: Double? = null,
        val inputShaftRpm: Double? = null,
        val outputShaftRpm: Double? = null,
        val driveBogieKg: Double? = null,
        val ishiftMode: String? = null,
    )

    private data class Sample(val t: Double, val rpm: Double, val speed: Double)

    private val ratioSeed = seedRatios.toMutableMap()
    private val ratioSamples = mutableMapOf<Int, ArrayDeque<Double>>()
    private val samples = ArrayDeque<Sample>()
    private var candidate: Int? = null
    private var candidateSinceMs: Long? = null

    fun update(input: Input): IShiftPredictionState {
        observeRatio(input.currentGear, input.actualGearRatio)

        val current = input.currentGear
        val selected = input.selectedGear
        val rpm = input.rpm
        val speed = input.speedKmh

        // A partir daqui não é mais previsão: a própria TCM já anunciou o destino.
        if (current != null && current > 0 && selected != null && selected > 0 && selected != current) {
            candidate = null
            candidateSinceMs = null
            return IShiftPredictionState(
                phase = IShiftPredictionPhase.CONFIRMED,
                currentGear = current,
                confirmedGear = selected,
                confidence = 1.0,
                projectedRpm = projectedRpm(current, selected, rpm),
            )
        }

        if (current == null || rpm == null || speed == null) {
            return IShiftPredictionState(currentGear = current)
        }

        val t = input.timeMs / 1000.0
        samples.add(Sample(t, rpm, speed))
        while (samples.isNotEmpty() && samples.first().t < t - WINDOW_SECONDS) samples.removeFirst()

        val drpmDt = slope(samples.map { it.t to it.rpm })
        val dvDt = slope(samples.map { it.t to it.speed })
        val candidateResult = chooseCandidate(current, rpm, input.actualGearRatio)
        val predicted = candidateResult?.first
        val projected = candidateResult?.second
        val fit = candidateResult?.third ?: 0.0

        // Recurso pensado para reduções em marcha alta. Não mostra previsão em baixa
        // velocidade/marcha para evitar ruído de manobra/arrancada.
        if (predicted == null || current < 7 || speed < 10.0) {
            clearCandidate()
            return IShiftPredictionState(
                currentGear = current,
                rpmSlopePerSecond = drpmDt,
                speedSlopeKmhPerSecond = dvDt,
            )
        }

        val pedal = input.pedalPct ?: 0.0
        val torque = input.torquePct ?: 0.0

        val pedalScore = clamp((pedal - 35.0) / 45.0)
        val torqueScore = clamp((torque - 50.0) / 45.0)
        val loadScore = 0.46 * pedalScore + 0.54 * torqueScore

        // Estudo do Log Mãe: reduções automáticas sob carga ocorreram tipicamente
        // quando a marcha atual estava perto de ~1000-1040 rpm. Projetamos quando
        // essa faixa será atingida sem mostrar countdown na UI.
        val secondsToLowRpm = if (drpmDt < -MIN_FALLING_RPM_PER_SECOND && rpm > LOW_RPM_TRIGGER) {
            (rpm - LOW_RPM_TRIGGER) / -drpmDt
        } else if (rpm <= LOW_RPM_TRIGGER + LOW_RPM_HOLD_MARGIN && loadScore >= 0.50) {
            0.0
        } else {
            null
        }

        val horizonScore = when {
            secondsToLowRpm == null -> 0.0
            secondsToLowRpm < 0.0 -> 0.0
            secondsToLowRpm <= PREDICTION_LOOKAHEAD_SECONDS ->
                // Mantém score alto em toda a janela de 0–5 s, com leve preferência
                // pela região de 2–4 s para antecipar e não apenas reagir.
                clamp(1.0 - abs(secondsToLowRpm - 3.2) / 5.2)
            else -> clamp(1.0 - (secondsToLowRpm - PREDICTION_LOOKAHEAD_SECONDS) / 4.0) * 0.35
        }

        val rpmFallScore = clamp((-drpmDt - 2.0) / 35.0)
        val speedFallScore = clamp((-dvDt - 0.01) / 0.35)
        val trendScore = 0.72 * rpmFallScore + 0.28 * speedFallScore

        // Se a RPM projetada 3,7 s à frente já cai na região baixa, reforça cedo.
        val projectedCurrentRpm = rpm + drpmDt * PREDICTION_HORIZON_SECONDS
        val lowRpmPressure = clamp((1125.0 - projectedCurrentRpm) / 180.0)

        val powerScore = input.powerKw?.let { clamp((it - 180.0) / 260.0) } ?: 0.0

        // Peso é apenas reforço leve: não deve impedir a previsão se o sinal faltar.
        val weightScore = input.driveBogieKg?.let { clamp((it - 16000.0) / 8000.0) } ?: 0.0

        // Slip durante troca pode gerar tendência falsa; penalização discreta.
        val slipPenalty = input.clutchSlipPct?.let { clamp((it - 8.0) / 35.0) * 0.10 } ?: 0.0

        // Coerência transmissão: quando input/output estão válidos, um estado
        // mecânico estável reforça que ainda estamos realmente antes da troca.
        val shaftStability = shaftStabilityScore(input.inputShaftRpm, input.outputShaftRpm)

        val risk = clamp(
            0.34 * loadScore +
                0.27 * horizonScore +
                0.14 * lowRpmPressure +
                0.10 * trendScore +
                0.08 * fit +
                0.03 * powerScore +
                0.02 * weightScore +
                0.02 * shaftStability -
                slipPenalty
        )

        // Score operacional, não probabilidade estatisticamente calibrada.
        val confidence = clamp(0.56 + 0.44 * risk)

        if (candidate != predicted) {
            candidate = predicted
            candidateSinceMs = input.timeMs
        }
        val stableMs = (input.timeMs - (candidateSinceMs ?: input.timeMs)).coerceAtLeast(0)

        // Gatilho principal: carga + aproximação da faixa de redução dentro de ~5 s.
        // Também aceita RPM já baixa sob carga, para situações em que a inclinação
        // estabiliza a rotação e dRPM/dt fica pequeno imediatamente antes da decisão.
        val underLoad = loadScore >= MIN_LOAD_SCORE || (pedal >= 50.0 && torque >= 65.0)
        val approaching = (secondsToLowRpm != null && secondsToLowRpm <= PREDICTION_LOOKAHEAD_SECONDS) ||
            (rpm <= LOW_RPM_TRIGGER + LOW_RPM_HOLD_MARGIN && lowRpmPressure >= 0.30)

        // Se o console está explicitamente em M, não antecipa decisão automática.
        // Em A funciona normalmente. Estado N recebido enquanto Current Gear > 0 e
        // veículo em movimento pode ser transitório/stale, então não deve matar o aviso.
        val automaticAllowed = input.ishiftMode != "M"

        val visible = automaticAllowed && underLoad && approaching &&
            confidence >= VISIBLE_CONFIDENCE_THRESHOLD && stableMs >= STABILITY_MS

        return IShiftPredictionState(
            phase = if (visible) IShiftPredictionPhase.PREDICTED else IShiftPredictionPhase.NONE,
            currentGear = current,
            predictedGear = predicted,
            confidence = confidence,
            projectedRpm = projected,
            riskScore = risk,
            rpmSlopePerSecond = drpmDt,
            speedSlopeKmhPerSecond = dvDt,
            stableMs = stableMs,
            estimatedSecondsToLowRpm = secondsToLowRpm,
        )
    }

    private fun clearCandidate() {
        candidate = null
        candidateSinceMs = null
    }

    private fun observeRatio(gear: Int?, ratio: Double?) {
        if (gear == null || gear <= 0 || ratio == null || ratio !in 0.05..30.0) return
        val q = ratioSamples.getOrPut(gear) { ArrayDeque() }
        q.add(ratio)
        while (q.size > 60) q.removeFirst()
    }

    /**
     * Relações observadas têm prioridade. Para relações ainda não vistas nesta
     * inicialização, preenche lacunas de 7ª–12ª por uma razão adjacente aproximada.
     * Isso resolve o bootstrap sem amarrar a previsão a uma tabela fixa: assim que
     * a PGN 61445 publica a relação real de cada marcha, ela substitui a estimativa.
     */
    private fun ratios(currentGear: Int? = null, currentActualRatio: Double? = null): Map<Int, Double> {
        val out = ratioSeed.toMutableMap()
        ratioSamples.forEach { (gear, values) ->
            if (values.isNotEmpty()) out[gear] = median(values.toList())
        }
        if (currentGear != null && currentActualRatio != null && currentActualRatio in 0.05..30.0) {
            out[currentGear] = currentActualRatio
        }

        // Bootstrap somente na faixa usada pelo preditor de estrada.
        for (gear in 7..12) {
            if (gear !in out) {
                val nearest = out.keys.filter { it in 6..12 }.minByOrNull { abs(it - gear) }
                if (nearest != null) {
                    val base = out.getValue(nearest)
                    val delta = gear - nearest
                    out[gear] = if (delta > 0) {
                        base / ADJACENT_RATIO_FACTOR.pow(delta.toDouble())
                    } else {
                        base * ADJACENT_RATIO_FACTOR.pow((-delta).toDouble())
                    }
                }
            }
        }
        return out
    }

    private fun chooseCandidate(
        currentGear: Int,
        rpm: Double,
        currentActualRatio: Double?,
    ): Triple<Int, Double, Double>? {
        val r = ratios(currentGear, currentActualRatio)
        val currentRatio = r[currentGear] ?: return null

        return r.entries.asSequence()
            .filter { (gear, ratio) -> gear in 1 until currentGear && ratio > currentRatio }
            .map { (gear, ratio) ->
                val projected = rpm * ratio / currentRatio
                Triple(gear, projected, 1.0 - abs(projected - TARGET_RPM) / 520.0)
            }
            .filter { (_, projected, _) -> projected in 700.0..1900.0 }
            .maxByOrNull { (_, _, fit) -> fit }
            ?.let { (gear, projected, fit) -> Triple(gear, projected, clamp(fit)) }
    }

    private fun projectedRpm(current: Int, target: Int, rpm: Double?): Double? {
        if (rpm == null) return null
        val r = ratios()
        val a = r[current] ?: return null
        val b = r[target] ?: return null
        return rpm * b / a
    }

    private fun shaftStabilityScore(inputRpm: Double?, outputRpm: Double?): Double {
        if (inputRpm == null || outputRpm == null || inputRpm <= 50.0 || outputRpm <= 1.0) return 0.0
        // Não tenta inferir relação física aqui; apenas evita usar valores zerados/soltos.
        return 1.0
    }

    private fun slope(points: List<Pair<Double, Double>>): Double {
        if (points.size < 3) return 0.0
        val x0 = points.first().first
        val xs = points.map { it.first - x0 }
        val ys = points.map { it.second }
        val mx = xs.average()
        val my = ys.average()
        val den = xs.sumOf { (it - mx) * (it - mx) }
        if (den <= 1e-9) return 0.0
        return xs.zip(ys).sumOf { (x, y) -> (x - mx) * (y - my) } / den
    }

    private fun median(values: List<Double>): Double {
        val s = values.sorted()
        return if (s.size % 2 == 1) s[s.size / 2] else (s[s.size / 2 - 1] + s[s.size / 2]) / 2.0
    }

    private fun clamp(v: Double) = v.coerceIn(0.0, 1.0)

    companion object {
        const val TARGET_RPM = 1200.0
        const val WINDOW_SECONDS = 5.0
        const val PREDICTION_HORIZON_SECONDS = 3.7
        const val PREDICTION_LOOKAHEAD_SECONDS = 5.0
        const val LOW_RPM_TRIGGER = 1025.0
        const val LOW_RPM_HOLD_MARGIN = 55.0
        const val MIN_FALLING_RPM_PER_SECOND = 2.0
        const val MIN_LOAD_SCORE = 0.48
        const val VISIBLE_CONFIDENCE_THRESHOLD = 0.78
        const val STABILITY_MS = 500L
        const val ADJACENT_RATIO_FACTOR = 1.275
    }
}
