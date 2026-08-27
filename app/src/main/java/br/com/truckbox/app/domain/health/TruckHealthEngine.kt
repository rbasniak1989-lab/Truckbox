package br.com.truckbox.app.domain.health

import br.com.truckbox.app.data.DataQuality
import br.com.truckbox.app.data.SensorValue
import br.com.truckbox.app.data.TruckState
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/**
 * TruckBox Health Engine v0.1.
 *
 * IMPORTANTE:
 * - Estes índices NÃO medem desgaste mecânico físico diretamente.
 * - Eles transformam sinais já mapeados em estado, estresse e tendência.
 * - Limites absolutos são provisórios de campo e devem ser refinados com logs reais.
 * - Onde possível, o motor aprende um baseline do próprio caminhão e compara o comportamento
 *   atual contra esse baseline em contexto semelhante.
 */
class TruckHealthEngine(initial: HealthLearningState = HealthLearningState()) {
    private var learning = initial

    private var lastGear: Int? = null
    private val gearChangeTimesMs = ArrayDeque<Long>()

    private var pendingShiftTarget: Int? = null
    private var pendingShiftStartedMs: Long? = null
    private var pendingShiftPeakSlip = 0.0

    fun snapshot(): HealthLearningState = learning

    fun resetLearning() {
        learning = HealthLearningState()
        lastGear = null
        gearChangeTimesMs.clear()
        pendingShiftTarget = null
        pendingShiftStartedMs = null
        pendingShiftPeakSlip = 0.0
    }

    fun update(state: TruckState, nowMs: Long, elapsedSeconds: Long = 1L): HealthState {
        val dt = elapsedSeconds.coerceIn(1L, 5L)

        observeGearChanges(state, nowMs)
        observeShift(state, nowMs)
        learnBaselines(state, dt)

        val engineStress = engineStress(state)
        val transmissionStress = transmissionStress(state)
        val drivetrainStress = drivetrainStress(state)

        val observed = learning.observedSeconds + dt
        learning = learning.copy(
            observedSeconds = observed,
            engineStressIntegral = learning.engineStressIntegral + engineStress * dt,
            engineSevereSeconds = learning.engineSevereSeconds + if (engineStress >= 80) dt else 0,
            transmissionStressIntegral = learning.transmissionStressIntegral + transmissionStress * dt,
            drivetrainStressIntegral = learning.drivetrainStressIntegral + drivetrainStress * dt,
        )

        val items = listOf(
            lubrication(state),
            thermalEngine(state),
            cooling(state),
            intakeTurbo(state),
            fuelSupply(state),
            engineStressItem(engineStress),
            clutch(state),
            transmissionThermal(state),
            shiftQuality(),
            gearHunting(),
            transmissionStressItem(transmissionStress),
            drivetrainStressItem(state, drivetrainStress),
        )

        return HealthState(
            items = items,
            engineCurrentStress = engineStress,
            engineAverageStress = average(learning.engineStressIntegral, observed),
            transmissionCurrentStress = transmissionStress,
            transmissionAverageStress = average(learning.transmissionStressIntegral, observed),
            drivetrainCurrentStress = drivetrainStress,
            drivetrainAverageStress = average(learning.drivetrainStressIntegral, observed),
            learning = learning,
            updatedAtMs = nowMs,
        )
    }

    private fun valid(s: SensorValue): Double? =
        s.value?.takeIf { it.isFinite() && s.quality == DataQuality.VALID }

    private fun learnBaselines(state: TruckState, dt: Long) {
        val rpm = valid(state.engine.rpm)
        val oilT = valid(state.engine.oilTempC)
        val oilP = valid(state.engine.oilPressureBar)
        val torque = valid(state.engine.actualTorquePct)
        val pedal = valid(state.engine.acceleratorPct)
        val boost = valid(state.engine.boostBar)
        val fuelP = valid(state.engine.fuelPressureBar)
        val speed = valid(state.speedKmh)

        // Lubrificação: aprende pressão normalizada por 1000 rpm somente com óleo quente
        // e motor em faixa estável. Assim comparamos condições semelhantes.
        if (rpm != null && oilT != null && oilP != null && torque != null &&
            rpm in 850.0..1650.0 && oilT in 85.0..112.0 && torque in 15.0..90.0 && oilP > 1.0
        ) {
            val ratio = oilP / (rpm / 1000.0)
            learning = learning.copy(
                oilPressureRatioBaseline = adaptiveBaseline(learning.oilPressureRatioBaseline, ratio, learning.oilPressureBaselineSamples, dt, 0.15),
                oilPressureBaselineSamples = learning.oilPressureBaselineSamples + dt,
            )
        }

        // Turbo/admissão: aprende boost por fração de torque em carga real e caminhão andando.
        if (rpm != null && torque != null && pedal != null && boost != null && speed != null &&
            rpm in 1000.0..1750.0 && torque in 55.0..100.0 && pedal >= 45.0 && speed >= 20.0 && boost > 0.15
        ) {
            val normalized = boost / (torque / 100.0).coerceAtLeast(0.35)
            learning = learning.copy(
                boostPerTorqueBaseline = adaptiveBaseline(learning.boostPerTorqueBaseline, normalized, learning.boostBaselineSamples, dt, 0.20),
                boostBaselineSamples = learning.boostBaselineSamples + dt,
            )
        }

        // Alimentação: pressão média do próprio caminhão em alta demanda.
        if (rpm != null && torque != null && pedal != null && fuelP != null &&
            rpm in 900.0..1800.0 && torque >= 55.0 && pedal >= 45.0 && fuelP > 0.2
        ) {
            learning = learning.copy(
                fuelPressureBaselineBar = adaptiveBaseline(learning.fuelPressureBaselineBar, fuelP, learning.fuelPressureBaselineSamples, dt, 0.15),
                fuelPressureBaselineSamples = learning.fuelPressureBaselineSamples + dt,
            )
        }
    }

    private fun lubrication(state: TruckState): HealthItem {
        val rpm = valid(state.engine.rpm)
        val oilP = valid(state.engine.oilPressureBar)
        val oilT = valid(state.engine.oilTempC)
        if (rpm == null || oilP == null) return unavailable("lubrication", HealthGroup.ENGINE, "Lubrificação do motor", "RPM + pressão do óleo")
        if (rpm < 500.0) return info("lubrication", HealthGroup.ENGINE, "Lubrificação do motor", "Motor sem carga", "Avaliação ativa com o motor em funcionamento.", "PGN 61444 + 65263")

        if (oilP < 1.0) return critical("lubrication", HealthGroup.ENGINE, "Lubrificação do motor", "Pressão muito baixa", "${f(oilP,1)} bar a ${f(rpm,0)} rpm. Confirmar no painel original e inspecionar.", "PGN 65263 + 61444")
        if (oilP < 1.5) return warning("lubrication", HealthGroup.ENGINE, "Lubrificação do motor", "Pressão baixa", "${f(oilP,1)} bar a ${f(rpm,0)} rpm. Limite provisório de campo.", "PGN 65263 + 61444")

        val baseline = learning.oilPressureRatioBaseline
        val currentRatio = oilP / (rpm / 1000.0).coerceAtLeast(0.5)
        if (oilT != null && oilT >= 80.0 && baseline != null && learning.oilPressureBaselineSamples >= 120) {
            val pct = currentRatio / baseline
            if (pct < 0.72) return critical("lubrication", HealthGroup.ENGINE, "Lubrificação do motor", "Abaixo do padrão histórico", "Pressão normalizada ${(pct*100).toInt()}% do baseline do próprio caminhão.", "pressão + RPM + temp. óleo")
            if (pct < 0.85) return warning("lubrication", HealthGroup.ENGINE, "Lubrificação do motor", "Tendência abaixo do padrão", "Pressão normalizada ${(pct*100).toInt()}% do baseline em óleo quente.", "pressão + RPM + temp. óleo")
            return ok("lubrication", HealthGroup.ENGINE, "Lubrificação do motor", "Dentro do padrão", "${f(oilP,1)} bar • ${(pct*100).toInt()}% do baseline", confidence(learning.oilPressureBaselineSamples), "pressão + RPM + temp. óleo")
        }
        return learning("lubrication", HealthGroup.ENGINE, "Lubrificação do motor", "Aprendendo pressão normal", "${f(oilP,1)} bar${oilT?.let { " • óleo ${f(it,0)} °C" } ?: ""}", learning.oilPressureBaselineSamples, "pressão + RPM + temp. óleo")
    }

    private fun thermalEngine(state: TruckState): HealthItem {
        val oil = valid(state.engine.oilTempC)
        val water = valid(state.engine.coolantTempC)
        if (oil == null && water == null) return unavailable("engine_thermal", HealthGroup.ENGINE, "Estresse térmico do motor", "temp. óleo + água")
        val maxSeverity = when {
            (oil ?: -999.0) >= 125.0 || (water ?: -999.0) >= 105.0 -> HealthSeverity.CRITICAL
            (oil ?: -999.0) >= 115.0 || (water ?: -999.0) >= 100.0 -> HealthSeverity.WARNING
            else -> HealthSeverity.OK
        }
        val text = "óleo ${oil?.let { f(it,0) } ?: "—"} °C • água ${water?.let { f(it,0) } ?: "—"} °C"
        return item("engine_thermal", HealthGroup.ENGINE, "Estresse térmico do motor", maxSeverity,
            when(maxSeverity){ HealthSeverity.CRITICAL -> "Temperatura crítica"; HealthSeverity.WARNING -> "Temperatura elevada"; else -> "Temperaturas normais" },
            "$text. Faixas provisórias TruckBox; prevalecem alertas OEM.", if(maxSeverity==HealthSeverity.OK) HealthConfidence.MEDIUM else HealthConfidence.HIGH, listOf("PGN 65262"))
    }

    private fun cooling(state: TruckState): HealthItem {
        val water = valid(state.engine.coolantTempC)
        val oil = valid(state.engine.oilTempC)
        val torque = valid(state.engine.actualTorquePct)
        if (water == null) return unavailable("cooling", HealthGroup.COOLING, "Arrefecimento", "temperatura da água")
        val delta = if (oil != null) oil - water else null
        val sev = when {
            water >= 105.0 -> HealthSeverity.CRITICAL
            water >= 100.0 -> HealthSeverity.WARNING
            delta != null && torque != null && torque >= 55.0 && delta > 32.0 -> HealthSeverity.WARNING
            else -> HealthSeverity.OK
        }
        val detail = buildString {
            append("Água ${f(water,0)} °C")
            if (oil != null) append(" • óleo ${f(oil,0)} °C")
            if (delta != null) append(" • Δ ${f(delta,0)} °C")
        }
        return item("cooling", HealthGroup.COOLING, "Arrefecimento", sev,
            if (sev == HealthSeverity.OK) "Comportamento térmico normal" else "Carga térmica elevada",
            detail, if (oil != null) HealthConfidence.MEDIUM else HealthConfidence.LOW,
            listOf("PGN 65262", "torque PGN 61444"))
    }

    private fun intakeTurbo(state: TruckState): HealthItem {
        val boost = valid(state.engine.boostBar)
        val intake = valid(state.engine.intakeAirTempC)
        val ambient = valid(state.engine.ambientTempC)
        val torque = valid(state.engine.actualTorquePct)
        val pedal = valid(state.engine.acceleratorPct)
        val rpm = valid(state.engine.rpm)
        val speed = valid(state.speedKmh)
        if (boost == null && intake == null) return unavailable("intake", HealthGroup.INTAKE, "Admissão / turbo", "boost + temperatura de admissão")

        val heatDelta = if (intake != null && ambient != null) intake - ambient else null
        if (heatDelta != null && speed != null && speed > 40.0 && heatDelta > 75.0)
            return critical("intake", HealthGroup.INTAKE, "Admissão / turbo", "Ar de admissão muito quente", "Δ admissão/ambiente ${f(heatDelta,0)} °C.", "PGN 65270 + 65269")
        if (heatDelta != null && speed != null && speed > 40.0 && heatDelta > 55.0)
            return warning("intake", HealthGroup.INTAKE, "Admissão / turbo", "Admissão aquecida", "Δ admissão/ambiente ${f(heatDelta,0)} °C.", "PGN 65270 + 65269")

        val demanding = torque != null && pedal != null && rpm != null && torque >= 55.0 && pedal >= 45.0 && rpm in 1000.0..1750.0
        val baseline = learning.boostPerTorqueBaseline
        if (demanding && boost != null && baseline != null && learning.boostBaselineSamples >= 120) {
            val expected = baseline * ((torque ?: 55.0) / 100.0).coerceAtLeast(0.35)
            val ratio = if (expected > 0.1) boost / expected else 1.0
            if (ratio < 0.60) return critical("intake", HealthGroup.INTAKE, "Admissão / turbo", "Boost muito abaixo do padrão", "${(ratio*100).toInt()}% do esperado para esta carga/RPM.", "boost + torque + RPM + pedal")
            if (ratio < 0.75) return warning("intake", HealthGroup.INTAKE, "Admissão / turbo", "Boost abaixo do padrão", "${(ratio*100).toInt()}% do baseline do caminhão.", "boost + torque + RPM + pedal")
            return ok("intake", HealthGroup.INTAKE, "Admissão / turbo", "Resposta dentro do padrão", "Boost ${f(boost,2)} bar • ${(ratio*100).toInt()}% do esperado", confidence(learning.boostBaselineSamples), "boost + torque + RPM + pedal")
        }
        return learning("intake", HealthGroup.INTAKE, "Admissão / turbo", "Aprendendo resposta do turbo", "Boost ${boost?.let { f(it,2) } ?: "—"} bar${heatDelta?.let { " • Δ ar ${f(it,0)} °C" } ?: ""}", learning.boostBaselineSamples, "boost + torque + RPM + pedal")
    }

    private fun fuelSupply(state: TruckState): HealthItem {
        val p = valid(state.engine.fuelPressureBar)
        val torque = valid(state.engine.actualTorquePct)
        val pedal = valid(state.engine.acceleratorPct)
        if (p == null) return unavailable("fuel_supply", HealthGroup.FUEL, "Alimentação de combustível", "pressão de combustível")
        val demanding = torque != null && pedal != null && torque >= 55.0 && pedal >= 45.0
        val base = learning.fuelPressureBaselineBar
        if (demanding && base != null && learning.fuelPressureBaselineSamples >= 120) {
            val ratio = p / base.coerceAtLeast(0.1)
            if (ratio < 0.70) return critical("fuel_supply", HealthGroup.FUEL, "Alimentação de combustível", "Pressão muito abaixo do padrão", "${f(p,2)} bar • ${(ratio*100).toInt()}% do baseline em carga.", "PGN 65263 + torque/pedal")
            if (ratio < 0.85) return warning("fuel_supply", HealthGroup.FUEL, "Alimentação de combustível", "Pressão abaixo do padrão", "${f(p,2)} bar • ${(ratio*100).toInt()}% do baseline.", "PGN 65263 + torque/pedal")
            return ok("fuel_supply", HealthGroup.FUEL, "Alimentação de combustível", "Pressão consistente", "${f(p,2)} bar • ${(ratio*100).toInt()}% do baseline", confidence(learning.fuelPressureBaselineSamples), "pressão + torque + pedal")
        }
        return learning("fuel_supply", HealthGroup.FUEL, "Alimentação de combustível", "Aprendendo pressão sob carga", "${f(p,2)} bar", learning.fuelPressureBaselineSamples, "pressão + torque + pedal")
    }

    private fun clutch(state: TruckState): HealthItem {
        val slip = valid(state.transmission.clutchSlipPct)
        val torque = valid(state.engine.actualTorquePct)
        val speed = valid(state.speedKmh)
        val gear = state.transmission.currentGear
        if (slip == null) return unavailable("clutch", HealthGroup.TRANSMISSION, "Embreagem / deslizamento", "clutch slip")
        val shifting = state.transmission.selectedGear != null && state.transmission.currentGear != null && state.transmission.selectedGear != state.transmission.currentGear
        val steadyLoaded = !shifting && (speed ?: 0.0) > 5.0 && (torque ?: 0.0) > 25.0 && (gear ?: 0) > 0
        if (steadyLoaded && slip >= 15.0) return critical("clutch", HealthGroup.TRANSMISSION, "Embreagem / deslizamento", "Deslizamento elevado em marcha estável", "${f(slip,1)}% sob carga.", "PGN 61442 + torque + marcha")
        if (steadyLoaded && slip >= 7.0) return warning("clutch", HealthGroup.TRANSMISSION, "Embreagem / deslizamento", "Deslizamento acima do esperado", "${f(slip,1)}% com marcha estabilizada.", "PGN 61442 + torque + marcha")
        return ok("clutch", HealthGroup.TRANSMISSION, "Embreagem / deslizamento", if (shifting) "Troca em andamento" else "Comportamento normal", "Slip ${f(slip,1)}%", HealthConfidence.MEDIUM, "PGN 61442")
    }

    private fun transmissionThermal(state: TruckState): HealthItem {
        val t = valid(state.transmission.oilTempC) ?: return unavailable("trans_thermal", HealthGroup.TRANSMISSION, "Temperatura da I-Shift", "temperatura do óleo da caixa")
        return when {
            t >= 115.0 -> critical("trans_thermal", HealthGroup.TRANSMISSION, "Temperatura da I-Shift", "Temperatura crítica", "${f(t,0)} °C", "PGN 65272 / TRF1")
            t >= 105.0 -> warning("trans_thermal", HealthGroup.TRANSMISSION, "Temperatura da I-Shift", "Temperatura elevada", "${f(t,0)} °C", "PGN 65272 / TRF1")
            else -> ok("trans_thermal", HealthGroup.TRANSMISSION, "Temperatura da I-Shift", "Temperatura normal", "${f(t,0)} °C", HealthConfidence.MEDIUM, "PGN 65272 / TRF1")
        }
    }

    private fun observeGearChanges(state: TruckState, nowMs: Long) {
        val gear = state.transmission.currentGear?.takeIf { it > 0 }
        if (gear != null && lastGear != null && gear != lastGear) {
            gearChangeTimesMs.add(nowMs)
            if (gearChangeTimesMs.size >= 8) {
                learning = learning.copy(huntingEvents = learning.huntingEvents + 1)
            }
        }
        if (gear != null) lastGear = gear
        while (gearChangeTimesMs.isNotEmpty() && nowMs - gearChangeTimesMs.first() > 60_000L) gearChangeTimesMs.removeFirst()
    }

    private fun observeShift(state: TruckState, nowMs: Long) {
        val current = state.transmission.currentGear
        val selected = state.transmission.selectedGear
        val slip = valid(state.transmission.clutchSlipPct) ?: 0.0

        if (current != null && selected != null && current > 0 && selected > 0 && selected != current) {
            if (pendingShiftTarget != selected || pendingShiftStartedMs == null) {
                pendingShiftTarget = selected
                pendingShiftStartedMs = nowMs
                pendingShiftPeakSlip = slip
            } else {
                pendingShiftPeakSlip = max(pendingShiftPeakSlip, slip)
            }
            return
        }

        val target = pendingShiftTarget
        val started = pendingShiftStartedMs
        if (target != null && started != null && current == target) {
            val durationSec = ((nowMs - started).coerceAtLeast(0L)) / 1000.0
            learning = learning.copy(
                shiftCount = learning.shiftCount + 1,
                slowShiftCount = learning.slowShiftCount + if (durationSec > 1.8) 1 else 0,
                shiftDurationEwmaSec = ewma(learning.shiftDurationEwmaSec, durationSec, 0.12),
                shiftPeakSlipEwmaPct = ewma(learning.shiftPeakSlipEwmaPct, pendingShiftPeakSlip, 0.12),
            )
        }
        pendingShiftTarget = null
        pendingShiftStartedMs = null
        pendingShiftPeakSlip = 0.0
    }

    private fun shiftQuality(): HealthItem {
        if (learning.shiftCount < 5) return learning("shift_quality", HealthGroup.TRANSMISSION, "Qualidade das trocas", "Aprendendo as trocas", "${learning.shiftCount} trocas observadas", learning.shiftCount, "Selected Gear + Current Gear + slip")
        val duration = learning.shiftDurationEwmaSec ?: 0.0
        val slip = learning.shiftPeakSlipEwmaPct ?: 0.0
        val slowPct = learning.slowShiftCount * 100.0 / learning.shiftCount.coerceAtLeast(1)
        val sev = when {
            duration > 2.5 || slip > 25.0 || slowPct > 35.0 -> HealthSeverity.CRITICAL
            duration > 1.8 || slip > 15.0 || slowPct > 20.0 -> HealthSeverity.WARNING
            else -> HealthSeverity.OK
        }
        return item("shift_quality", HealthGroup.TRANSMISSION, "Qualidade das trocas", sev,
            when(sev){HealthSeverity.CRITICAL -> "Trocas muito fora do padrão"; HealthSeverity.WARNING -> "Trocas merecem acompanhamento"; else -> "Trocas consistentes"},
            "média ${f(duration,2)} s • pico slip médio ${f(slip,1)}% • lentas ${f(slowPct,0)}%",
            confidence(learning.shiftCount), listOf("PGN 61445", "PGN 61442"))
    }

    private fun gearHunting(): HealthItem {
        val count = gearChangeTimesMs.size
        val sev = when {
            count >= 12 -> HealthSeverity.CRITICAL
            count >= 8 -> HealthSeverity.WARNING
            else -> HealthSeverity.OK
        }
        return item("gear_hunting", HealthGroup.TRANSMISSION, "Caça de marchas", sev,
            if (sev == HealthSeverity.OK) "Sem caça excessiva" else "Muitas trocas em pouco tempo",
            "$count trocas nos últimos 60 s", HealthConfidence.MEDIUM, listOf("PGN 61445"))
    }

    private fun engineStress(state: TruckState): Int {
        val torque = (valid(state.engine.actualTorquePct) ?: 0.0).coerceIn(0.0, 100.0)
        val rpm = valid(state.engine.rpm) ?: 0.0
        val oil = valid(state.engine.oilTempC)
        val water = valid(state.engine.coolantTempC)
        val torquePart = torque * 0.45
        val oilPart = ((oil ?: 90.0) - 95.0).coerceAtLeast(0.0).coerceAtMost(30.0) / 30.0 * 25.0
        val waterPart = ((water ?: 85.0) - 90.0).coerceAtLeast(0.0).coerceAtMost(20.0) / 20.0 * 15.0
        val rpmPart = ((rpm - 1650.0).coerceAtLeast(0.0).coerceAtMost(600.0) / 600.0) * 15.0
        return (torquePart + oilPart + waterPart + rpmPart).toInt().coerceIn(0, 100)
    }

    private fun transmissionStress(state: TruckState): Int {
        val torque = (valid(state.engine.actualTorquePct) ?: 0.0).coerceIn(0.0, 100.0)
        val slip = (valid(state.transmission.clutchSlipPct) ?: 0.0).coerceIn(0.0, 30.0)
        val temp = valid(state.transmission.oilTempC) ?: 80.0
        val shifting = state.transmission.selectedGear != null && state.transmission.currentGear != null && state.transmission.selectedGear != state.transmission.currentGear
        val score = torque * 0.45 + slip / 30.0 * 25.0 + ((temp - 90.0).coerceAtLeast(0.0).coerceAtMost(30.0)/30.0*20.0) + if (shifting) 10.0 else 0.0
        return score.toInt().coerceIn(0,100)
    }

    private fun drivetrainStress(state: TruckState): Int {
        val torquePct = (valid(state.engine.actualTorquePct) ?: 0.0).coerceIn(0.0,100.0)
        val ratio = state.transmission.actualGearRatio?.takeIf { it.isFinite() && it > 0.0 } ?: 1.0
        val combinationKg = valid(state.weights.combinationKg)
        val bogieKg = valid(state.weights.driveBogieKg)
        val weightFactor = when {
            combinationKg != null -> (combinationKg / 60_000.0).coerceIn(0.25, 1.25)
            bogieKg != null -> (bogieKg / 28_000.0).coerceIn(0.25, 1.25)
            else -> 0.65
        }
        val engineNm = 2643.0 * torquePct / 100.0
        val gearboxOutputNm = engineNm * ratio
        val mechanical = (gearboxOutputNm / 18_000.0).coerceIn(0.0,1.2)
        return ((mechanical * 70.0) + (weightFactor * 30.0)).toInt().coerceIn(0,100)
    }

    private fun engineStressItem(score: Int): HealthItem {
        val avg = average(learning.engineStressIntegral, learning.observedSeconds.coerceAtLeast(1))
        val sev = when { score >= 90 -> HealthSeverity.CRITICAL; score >= 75 -> HealthSeverity.WARNING; else -> HealthSeverity.OK }
        return item("engine_stress", HealthGroup.ENGINE, "Índice de estresse do motor", sev,
            stressHeadline(score), "agora $score/100 • média acumulada $avg/100 • severo ${learning.engineSevereSeconds}s",
            confidence(learning.observedSeconds), listOf("torque", "RPM", "temp. óleo", "temp. água"), score)
    }

    private fun transmissionStressItem(score: Int): HealthItem {
        val avg = average(learning.transmissionStressIntegral, learning.observedSeconds.coerceAtLeast(1))
        val sev = when { score >= 90 -> HealthSeverity.CRITICAL; score >= 75 -> HealthSeverity.WARNING; else -> HealthSeverity.OK }
        return item("trans_stress", HealthGroup.TRANSMISSION, "Estresse da transmissão", sev,
            stressHeadline(score), "agora $score/100 • média acumulada $avg/100",
            confidence(learning.observedSeconds), listOf("torque", "slip", "temp. caixa", "troca"), score)
    }

    private fun drivetrainStressItem(state: TruckState, score: Int): HealthItem {
        val avg = average(learning.drivetrainStressIntegral, learning.observedSeconds.coerceAtLeast(1))
        val torquePct = valid(state.engine.actualTorquePct)
        val ratio = state.transmission.actualGearRatio
        val sev = when { score >= 90 -> HealthSeverity.CRITICAL; score >= 75 -> HealthSeverity.WARNING; else -> HealthSeverity.OK }
        return item("drivetrain_stress", HealthGroup.DRIVETRAIN, "Trem de força / diferencial", sev,
            stressHeadline(score), "esforço estimado $score/100 • média $avg/100${torquePct?.let { " • torque ${f(it,0)}%" } ?: ""}${ratio?.let { " • relação ${f(it,3)}" } ?: ""}. Não mede desgaste físico do diferencial.",
            confidence(learning.observedSeconds), listOf("torque", "relação da marcha", "peso"), score)
    }

    private fun stressHeadline(score: Int): String = when {
        score >= 90 -> "Esforço severo"
        score >= 75 -> "Esforço elevado"
        score >= 45 -> "Esforço moderado"
        else -> "Esforço baixo"
    }

    private fun average(integral: Double, seconds: Long): Int = if (seconds <= 0) 0 else (integral / seconds).toInt().coerceIn(0,100)

    private fun confidence(samples: Long): HealthConfidence = when {
        samples >= 900 -> HealthConfidence.HIGH
        samples >= 120 -> HealthConfidence.MEDIUM
        else -> HealthConfidence.LOW
    }

    private fun adaptiveBaseline(old: Double?, value: Double, samples: Long, dt: Long, gatePct: Double): Double {
        if (old == null || !old.isFinite()) return value
        // Fase inicial: aprende rápido. Depois de estabilizado, aprende MUITO devagar e
        // não absorve uma queda grande como se ela fosse o novo normal. Isso preserva
        // utilidade para tendência de meses/quilômetros.
        if (samples < 120) return ewma(old, value, 0.02 * dt)
        val deviation = abs(value / old.coerceAtLeast(0.0001) - 1.0)
        if (deviation > gatePct) return old
        return ewma(old, value, 0.0005 * dt)
    }

    private fun ewma(old: Double?, value: Double, alphaRaw: Double): Double {
        val alpha = alphaRaw.coerceIn(0.0001, 0.35)
        return if (old == null || !old.isFinite()) value else old + alpha * (value - old)
    }

    private fun f(v: Double, decimals: Int): String = "% .${decimals}f".format(java.util.Locale.US, v).trim()

    private fun unavailable(id:String, group:HealthGroup, title:String, base:String) =
        item(id, group, title, HealthSeverity.UNAVAILABLE, "Sem sinal suficiente", "Aguardando $base.", HealthConfidence.LOW, listOf(base))

    private fun learning(id:String, group:HealthGroup, title:String, headline:String, detail:String, samples:Long, base:String) =
        item(id, group, title, HealthSeverity.LEARNING, headline, "$detail • amostras ${samples}s", confidence(samples), listOf(base))

    private fun ok(id:String, group:HealthGroup, title:String, headline:String, detail:String, confidence:HealthConfidence, base:String) =
        item(id, group, title, HealthSeverity.OK, headline, detail, confidence, listOf(base))

    private fun info(id:String, group:HealthGroup, title:String, headline:String, detail:String, base:String) =
        item(id, group, title, HealthSeverity.INFO, headline, detail, HealthConfidence.LOW, listOf(base))

    private fun warning(id:String, group:HealthGroup, title:String, headline:String, detail:String, base:String) =
        item(id, group, title, HealthSeverity.WARNING, headline, detail, HealthConfidence.MEDIUM, listOf(base))

    private fun critical(id:String, group:HealthGroup, title:String, headline:String, detail:String, base:String) =
        item(id, group, title, HealthSeverity.CRITICAL, headline, detail, HealthConfidence.HIGH, listOf(base))

    private fun item(
        id:String, group:HealthGroup, title:String, severity:HealthSeverity, headline:String, detail:String,
        confidence:HealthConfidence, basedOn:List<String>, score:Int? = null
    ) = HealthItem(id, group, title, severity, headline, detail, score, confidence, basedOn)
}
