package br.com.truckbox.app.parser

import br.com.truckbox.app.data.*
import br.com.truckbox.app.domain.transmission.IShiftPredictor
import kotlin.math.max

/**
 * Porta Kotlin de campo do Parser J1939 V1 para os sinais usados pelo app.
 * O Python continua sendo a implementação canônica de referência.
 *
 * Importante: mensagens TP multipacket (60416/60160) ainda não são remontadas nesta v0.3.
 * Nenhum sinal essencial da Dashboard/Consumo depende delas.
 */
class TruckBoxJ1939Decoder {
    private var referenceTorqueNm = REFERENCE_TORQUE_NM
    private var fuelAccumulator = FuelPowerAccumulator()
    private var rolling50 = RollingDistanceKm(50.0)
    private var rolling100 = RollingDistanceKm(100.0)
    private var restoredLast100Kml: Double? = null
    private var ishiftPredictor = IShiftPredictor()
    private var lastEspTimestampMs: Long? = null
    private var weight1fKg: Double? = null
    private var weight2fKg: Double? = null
    private var firstDistanceKm: Double? = null
    private var restoredSessionDistanceKm: Double = 0.0
    private var lastPredictionInputMs: Long = Long.MIN_VALUE

    private val aliasByPgnSa = buildMap<Pair<Int, Int>, Int> {
        for (sa in 0..5) put(61456 to sa, 61440 + sa)
        put(65150 to 108, 65132)
        put(65246 to 223, 65247)
        put(65246 to 193, 65217)
        put(65214 to 191, 65215)
        put(65118 to 86, 65110)
        put(65311 to 2, 65282)
        put(65311 to 6, 65286)
        put(65311 to 12, 65292)
        put(65311 to 15, 65295)
        put(65343 to 37, 65317)
        put(65343 to 40, 65320)
        put(65343 to 41, 65321)
        put(65343 to 43, 65323)
        put(65343 to 60, 65340)
        put(65278 to 230, 65254)
        put(65278 to 234, 65258)
        put(65278 to 238, 65262)
        put(65278 to 239, 65263)
        put(65278 to 241, 65265)
        put(65278 to 245, 65269)
        put(65278 to 246, 65270)
        put(65278 to 248, 65272)
    }


    /** Restaura os acumuladores salvos antes de o app/caminhão desligar. */
    fun restoreSession(
        sessionLiters: Double,
        sessionDistanceKm: Double,
        lastOdometerKm: Double?,
        best50Kml: Double?,
        last100Kml: Double?,
    ) {
        fuelAccumulator = FuelPowerAccumulator(initialLiters = sessionLiters)
        rolling50 = RollingDistanceKm(50.0, initialBest = best50Kml)
        rolling100 = RollingDistanceKm(100.0)
        restoredLast100Kml = last100Kml
        restoredSessionDistanceKm = sessionDistanceKm.coerceAtLeast(0.0)
        firstDistanceKm = lastOdometerKm?.minus(restoredSessionDistanceKm)
    }

    /**
     * Sincroniza os acumuladores persistentes do TruckBox Core. Isto recupera o
     * período em que a multimídia/app ficou desligado. O Core é a autoridade dos
     * totais; o parser Android continua responsável pelos sinais ao vivo.
     */
    fun syncCoreSession(
        state: TruckState,
        coreFuelLiters: Double?,
        coreDistanceKm: Double?,
        coreOdometerKm: Double?,
    ): TruckState {
        val fuel = coreFuelLiters?.takeIf { it.isFinite() && it >= 0.0 } ?: state.fuel.sessionLiters
        val distance = coreDistanceKm?.takeIf { it.isFinite() && it >= 0.0 } ?: state.fuel.sessionDistanceKm
        fuelAccumulator = FuelPowerAccumulator(initialLiters = fuel)
        restoredSessionDistanceKm = distance
        if (coreOdometerKm != null && coreOdometerKm.isFinite()) {
            firstDistanceKm = coreOdometerKm - distance
        }
        val avg = if (fuel > 0.02) distance / fuel else null
        return state.copy(
            odometerKm = if (coreOdometerKm != null && coreOdometerKm.isFinite())
                SensorValue(coreOdometerKm, DataQuality.VALID, android.os.SystemClock.elapsedRealtime(), "CORE/api/state")
            else state.odometerKm,
            fuel = state.fuel.copy(
                fuelReady = coreFuelLiters != null || state.fuel.fuelReady,
                distanceReady = coreDistanceKm != null || state.fuel.distanceReady,
                sessionLiters = fuel,
                sessionDistanceKm = distance,
                sessionAverageKml = avg,
            ),
        )
    }

    /** Zera somente os acumuladores da sessão de campo. Não altera sensores ao vivo. */
    fun resetSession(state: TruckState): TruckState {
        fuelAccumulator = FuelPowerAccumulator()
        rolling50 = RollingDistanceKm(50.0)
        rolling100 = RollingDistanceKm(100.0)
        restoredLast100Kml = null
        restoredSessionDistanceKm = 0.0
        firstDistanceKm = null
        return state.copy(
            fuel = state.fuel.copy(
                fuelReady = false,
                distanceReady = false,
                sessionLiters = 0.0,
                sessionDistanceKm = 0.0,
                sessionAverageKml = null,
                last50KmAverageKml = null,
                best50KmAverageKml = null,
                last100KmAverageKml = null,
            ),
            analytics = DrivingAnalyticsState(),
        )
    }

    fun decode(frame: J1939Frame, state: TruckState, receivedAtMs: Long): TruckState {
        val prevTs = lastEspTimestampMs
        if (prevTs != null && frame.timestampMs + 1000L < prevTs) {
            ishiftPredictor = IShiftPredictor()
            lastPredictionInputMs = Long.MIN_VALUE
        }
        lastEspTimestampMs = frame.timestampMs
        if (aliasByPgnSa.containsKey(frame.pgn to frame.sa)) return state
        var s = state
        val d = frame.data

        when (frame.pgn) {
            61443 -> if (d.size >= 2) {
                s = s.copy(engine = s.engine.copy(acceleratorPct = valid(d.u8(1) * 0.4, receivedAtMs, "61443/B1")))
            }

            61444 -> if (d.size >= 5) {
                val tqPct = d.u8(2) - 125.0
                val rpm = d.le16(3) * 0.125
                val tqNm = referenceTorqueNm * max(tqPct, 0.0) / 100.0
                val powerKw = tqNm * rpm / 9550.0
                val lph = fuelAccumulator.ingest(frame, referenceTorqueNm)
                s = s.copy(
                    engine = s.engine.copy(
                        rpm = valid(rpm, receivedAtMs, "61444/B3:B4"),
                        actualTorquePct = valid(tqPct, receivedAtMs, "61444/B2"),
                        powerKw = valid(powerKw, receivedAtMs, "61444/derived"),
                    ),
                    fuel = s.fuel.copy(
                        fuelRateLph = if (lph != null) valid(lph, receivedAtMs, "61444/derived") else s.fuel.fuelRateLph,
                        fuelReady = true,
                        sessionLiters = fuelAccumulator.liters,
                    ),
                )
            }

            61445 -> if (d.size >= 4) {
                val selected = if (d.u8(0) != 0xFF) d.u8(0) - 125 else s.transmission.selectedGear
                val ratioRaw = d.le16(1)
                val ratio = if (ratioRaw != 0xFFFF) ratioRaw * 0.001 else s.transmission.actualGearRatio
                val current = if (d.u8(3) != 0xFF) d.u8(3) - 125 else s.transmission.currentGear
                s = s.copy(transmission = s.transmission.copy(currentGear = current, selectedGear = selected, actualGearRatio = ratio))
            }

            61442 -> if (d.size >= 7) {
                val outRaw = d.le16(1)
                val slip = if (d.u8(3) != 0xFF) valid(d.u8(3) * 0.4, receivedAtMs, "61442/B3") else s.transmission.clutchSlipPct
                val inRaw = d.le16(5)
                s = s.copy(transmission = s.transmission.copy(
                    outputShaftRpm = if (outRaw != 0xFFFF) plausibleRpm(outRaw * 0.125, receivedAtMs, "61442/B1:B2") else s.transmission.outputShaftRpm,
                    clutchSlipPct = slip,
                    inputShaftRpm = if (inRaw != 0xFFFF) plausibleRpm(inRaw * 0.125, receivedAtMs, "61442/B5:B6") else s.transmission.inputShaftRpm,
                ))
            }

            65265 -> if (d.size >= 3) {
                s = s.copy(speedKmh = valid(d.le16(1) / 256.0, receivedAtMs, "65265/B1:B2"))
            }

            65217 -> if (d.size >= 8) {
                val totalKm = d.le32(0) * 0.005
                // Se voltamos de um desligamento e ainda não havia odômetro salvo,
                // preserva a distância já acumulada usando o primeiro frame novo como referência.
                if (firstDistanceKm == null) firstDistanceKm = totalKm - restoredSessionDistanceKm
                val sessionDistance = (totalKm - (firstDistanceKm ?: totalKm)).coerceAtLeast(0.0)
                val roll50 = rolling50.update(totalKm, fuelAccumulator.liters)
                val roll100 = rolling100.update(totalKm, fuelAccumulator.liters)
                val avg = if (fuelAccumulator.liters > 0.02) sessionDistance / fuelAccumulator.liters else null
                // Depois de um restart, preservamos visualmente o último 100 km salvo até
                // a nova janela completar 100 km. Assim a tela não "zera", mas o valor
                // novo só substitui o checkpoint quando a janela móvel estiver completa.
                val last100 = roll100.first ?: restoredLast100Kml ?: s.fuel.last100KmAverageKml
                if (roll100.first != null) restoredLast100Kml = null
                s = s.copy(
                    odometerKm = valid(totalKm, receivedAtMs, "65217/B0:B3"),
                    fuel = s.fuel.copy(
                        distanceReady = true,
                        sessionDistanceKm = sessionDistance,
                        sessionAverageKml = avg,
                        last50KmAverageKml = roll50.first ?: s.fuel.last50KmAverageKml,
                        best50KmAverageKml = roll50.second,
                        last100KmAverageKml = last100,
                    ),
                )
            }

            65262 -> if (d.size >= 4) {
                val coolant = if (d.u8(0) != 0xFF) valid(d.u8(0) - 40.0, receivedAtMs, "65262/B0") else s.engine.coolantTempC
                val rawOil = d.le16(2)
                val oil = if (rawOil != 0xFFFF) plausibleTemp(rawOil * 0.03125 - 273.0, receivedAtMs, "65262/B2:B3") else s.engine.oilTempC
                s = s.copy(engine = s.engine.copy(coolantTempC = coolant, oilTempC = oil))
            }

            65263 -> if (d.size >= 4) {
                val fuelP = if (d.u8(0) != 0xFF) valid(d.u8(0) * 0.04, receivedAtMs, "65263/B0") else s.engine.fuelPressureBar
                val oilLevel = if (d.u8(2) != 0xFF) valid(d.u8(2) * 0.4, receivedAtMs, "65263/B2") else s.engine.oilLevelPct
                val oilP = if (d.u8(3) != 0xFF) valid(d.u8(3) * 0.04, receivedAtMs, "65263/B3") else s.engine.oilPressureBar
                s = s.copy(engine = s.engine.copy(fuelPressureBar = fuelP, oilLevelPct = oilLevel, oilPressureBar = oilP))
            }

            65270 -> if (d.size >= 3) {
                // IC1 validado no TruckBox:
                // B1 = Intake Manifold Pressure (SPN 102), 2 kPa/bit -> 0,02 bar operacional
                // B2 = Intake Manifold Temperature (SPN 105), 1 °C/bit, offset -40 °C
                val boost = if (d.u8(1) != 0xFF) valid(d.u8(1) * 0.02, receivedAtMs, "65270/B1-SPN102") else s.engine.boostBar
                val intake = if (d.u8(2) != 0xFF) plausibleTemp(d.u8(2) - 40.0, receivedAtMs, "65270/B2-SPN105") else s.engine.intakeAirTempC
                s = s.copy(engine = s.engine.copy(boostBar = boost, intakeAirTempC = intake))
            }

            65269 -> if (d.size >= 5) {
                // AMB validado no TruckBox: B3:B4 LE = Ambient Air Temperature,
                // resolução 0,03125 °C/bit, offset -273 °C.
                val rawAmbient = d.le16(3)
                val ambient = if (rawAmbient != 0xFFFF) plausibleTemp(rawAmbient * 0.03125 - 273.0, receivedAtMs, "65269/B3:B4") else s.engine.ambientTempC
                s = s.copy(engine = s.engine.copy(ambientTempC = ambient))
            }

            65272 -> if (d.size >= 6) {
                val raw = d.le16(4)
                if (raw != 0xFFFF) s = s.copy(transmission = s.transmission.copy(oilTempC = plausibleTemp(raw * 0.03125 - 273.0, receivedAtMs, "65272/B4:B5")))
            }

            65258 -> if (d.size >= 3) {
                val loc = d.u8(0)
                val raw = d.le16(1)
                if (raw != 0xFFFF) {
                    val kg = raw * 0.5
                    if (loc == 0x1F) weight1fKg = kg
                    if (loc == 0x2F) weight2fKg = kg
                    val vals = listOfNotNull(weight1fKg, weight2fKg)
                    if (vals.isNotEmpty()) {
                        val bogie = vals.average()
                        s = s.copy(weights = s.weights.copy(driveBogieKg = valid(bogie, receivedAtMs, "65258/1F+2F redundant")))
                    }
                }
            }

            65295 -> if (d.size >= 6 && d.u8(5) != 0xFF) {
                val kg = d.u8(5) * 1000.0
                if (kg in 10000.0..100000.0) s = s.copy(weights = s.weights.copy(combinationKg = valid(kg, receivedAtMs, "65295/B5")))
            }

            65321 -> if (d.size >= 8) {
                val text = d.take(8).map { (it.toInt() and 0xFF).toChar() }.joinToString("")
                s = s.copy(transmission = s.transmission.copy(iRollActive = text == " AN E+00"))
            }

            65323 -> if (d.size >= 5) {
                val cruise = d.u8(0) == 0xDF
                val veb = (d.u8(1) shr 5) and 0x07
                s = s.copy(transmission = s.transmission.copy(cruiseActive = cruise, vebStageRaw = veb))
            }

            65286 -> if (d.size >= 2) {
                val mode = when (d.u8(0)) { 0xF2 -> "N"; 0xF3 -> "A"; 0xF4 -> "M"; else -> s.transmission.ishiftMode }
                s = s.copy(transmission = s.transmission.copy(ishiftMode = mode))
            }
        }

        val shouldPredict = frame.pgn == 61445 || lastPredictionInputMs == Long.MIN_VALUE || frame.timestampMs - lastPredictionInputMs >= 100L
        if (shouldPredict) {
            lastPredictionInputMs = frame.timestampMs
            val p = ishiftPredictor.update(IShiftPredictor.Input(
                timeMs = frame.timestampMs,
                currentGear = s.transmission.currentGear,
                selectedGear = s.transmission.selectedGear,
                actualGearRatio = s.transmission.actualGearRatio,
                rpm = s.engine.rpm.value,
                speedKmh = s.speedKmh.value,
                torquePct = s.engine.actualTorquePct.value,
                pedalPct = s.engine.acceleratorPct.value,
                powerKw = s.engine.powerKw.value,
                clutchSlipPct = s.transmission.clutchSlipPct.value,
                inputShaftRpm = s.transmission.inputShaftRpm.value,
                outputShaftRpm = s.transmission.outputShaftRpm.value,
                driveBogieKg = s.weights.driveBogieKg.value,
                ishiftMode = s.transmission.ishiftMode,
            ))
            s = s.copy(transmission = s.transmission.copy(prediction = IShiftPredictionUi(
                phase = p.phase.name,
                predictedGear = p.predictedGear,
                confirmedGear = p.confirmedGear,
                score = p.confidence,
                projectedRpm = p.projectedRpm,
                riskScore = p.riskScore,
                rpmSlopePerSecond = p.rpmSlopePerSecond,
                speedSlopeKmhPerSecond = p.speedSlopeKmhPerSecond,
                stableMs = p.stableMs,
                estimatedSecondsToLowRpm = p.estimatedSecondsToLowRpm,
            )))
        }
        return s
    }

    private fun valid(v: Double, now: Long, source: String) = SensorValue(v, DataQuality.VALID, now, source)
    private fun plausibleTemp(v: Double, now: Long, source: String) =
        if (v in -50.0..220.0) valid(v, now, source) else SensorValue(v, DataQuality.SUSPECT, now, source)
    private fun plausibleRpm(v: Double, now: Long, source: String) =
        if (v in 0.0..3000.0) valid(v, now, source) else SensorValue(v, DataQuality.SUSPECT, now, source)

    companion object {
        const val REFERENCE_TORQUE_NM = 2643.0
    }
}

class FuelPowerAccumulator(
    private val calibrationFactor: Double = 0.9720031611625598,
    initialLiters: Double = 0.0,
) {
    var liters: Double = initialLiters.coerceAtLeast(0.0)
        private set
    private var lastTimestampMs: Long? = null

    fun ingest(frame: J1939Frame, referenceTorqueNm: Double): Double? {
        if (frame.pgn != 61444 || frame.data.size < 5) return null
        val tqPct = frame.data.u8(2) - 125.0
        val rpm = frame.data.le16(3) * 0.125
        val torqueNm = referenceTorqueNm * max(tqPct, 0.0) / 100.0
        val powerKw = torqueNm * rpm / 9550.0
        val lph = powerKw * 0.25 * calibrationFactor
        val last = lastTimestampMs
        if (last != null) {
            val dt = (frame.timestampMs - last) / 1000.0
            if (dt in 0.000001..1.0) liters += lph * dt / 3600.0
        }
        lastTimestampMs = frame.timestampMs
        return lph
    }
}

private class RollingDistanceKm(
    private val windowKm: Double,
    initialBest: Double? = null,
) {
    private data class Point(val distanceKm: Double, val liters: Double)
    private val points = ArrayDeque<Point>()
    private var best: Double? = initialBest

    fun update(distanceKm: Double, liters: Double): Pair<Double?, Double?> {
        if (points.isEmpty() || distanceKm - points.last().distanceKm >= 0.05) {
            points.add(Point(distanceKm, liters))
        }
        while (points.size > 2 && distanceKm - points[1].distanceKm >= windowKm) {
            points.removeFirst()
        }
        val start = points.firstOrNull() ?: return null to best
        val dd = distanceKm - start.distanceKm
        val dl = liters - start.liters
        val current = if (dd >= windowKm - 0.5 && dl > 0.05) dd / dl else null
        if (current != null && (best == null || current > best!!)) best = current
        return current to best
    }
}
