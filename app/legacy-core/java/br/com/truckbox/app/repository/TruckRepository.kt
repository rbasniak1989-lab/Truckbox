package br.com.truckbox.app.repository

import android.content.Context
import android.os.SystemClock
import br.com.truckbox.app.data.*
import br.com.truckbox.app.domain.health.HealthState
import br.com.truckbox.app.domain.health.TruckHealthEngine
import br.com.truckbox.app.parser.Tb1LineParser
import br.com.truckbox.app.parser.TruckBoxJ1939Decoder
import br.com.truckbox.app.transport.Esp32TcpClient
import br.com.truckbox.app.transport.CoreStateClient
import br.com.truckbox.app.preferences.TruckBoxPreferences
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class TruckRepository(private val context: Context) {
    private val sessionStore = FieldSessionStore(context)
    private val restored = sessionStore.load()

    private val lock = Any()
    private val healthEngine = TruckHealthEngine(restored.healthLearning)

    private val decoder = TruckBoxJ1939Decoder().apply {
        restoreSession(
            sessionLiters = restored.sessionLiters,
            sessionDistanceKm = restored.sessionDistanceKm,
            lastOdometerKm = restored.lastOdometerKm,
            best50Kml = restored.best50KmAverageKml,
            last100Kml = restored.last100KmAverageKml,
        )
    }

    private var working = TruckState(
        odometerKm = restored.lastOdometerKm?.let {
            SensorValue(it, DataQuality.STALE, 0L, "checkpoint")
        } ?: SensorValue(),
        connection = ConnectionState(sessionSeconds = restored.sessionSeconds),
        fuel = FuelState(
            fuelReady = restored.fuelReady,
            distanceReady = restored.distanceReady,
            sessionLiters = restored.sessionLiters,
            sessionDistanceKm = restored.sessionDistanceKm,
            sessionAverageKml = if (restored.sessionLiters > 0.02) restored.sessionDistanceKm / restored.sessionLiters else null,
            last50KmAverageKml = restored.last50KmAverageKml,
            best50KmAverageKml = restored.best50KmAverageKml,
            last100KmAverageKml = restored.last100KmAverageKml,
        ),
        operation = OperationState(restored.operationMode, restored.activeTripId),
        analytics = restored.analytics,
        health = HealthState(learning = restored.healthLearning),
    )

    private val _state = MutableStateFlow(working)
    val state: StateFlow<TruckState> = _state.asStateFlow()

    private val rawBuffer = RawRingBuffer()
    private val pgnCounts = mutableMapOf<Int, Long>()
    private val recent = ArrayDeque<RawFrameSummary>()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    // sessionStart mede somente o tempo deste processo. sessionBaseSeconds veio do checkpoint.
    private var sessionBaseSeconds = restored.sessionSeconds
    private var sessionStart = SystemClock.elapsedRealtime()
    private var lastFrameReceivedAt = 0L
    private var lastPublishAt = 0L
    private var fpsWindowStart = SystemClock.elapsedRealtime()
    private var fpsWindowFrames = 0L
    private var fps = 0.0
    private var totalFrames = 0L
    private var invalidLines = 0L
    private var reconnects = 0L
    private var wasTcp = false
    private var lastAnalyticsTick = SystemClock.elapsedRealtime()

    private val client = Esp32TcpClient(
        context = context,
        onLine = ::onRawLine,
        onStatus = ::onConnection,
    )

    private val prefs = TruckBoxPreferences(context)
    private val coreStateClient = CoreStateClient(context, prefs)

    fun start() {
        client.start()

        // O Core é a autoridade dos acumuladores. A cada 5 s recuperamos
        // distância/combustível persistentes, inclusive o que ocorreu com a tela desligada.
        scope.launch(Dispatchers.IO) {
            delay(1200)
            while (isActive) {
                coreStateClient.fetch()?.let { core ->
                    synchronized(lock) {
                        working = decoder.syncCoreSession(
                            state = working,
                            coreFuelLiters = core.fuelLiters,
                            coreDistanceKm = core.distanceKm,
                            coreOdometerKm = core.odometerKm,
                        )
                        publish(force = true)
                    }
                }
                delay(5000)
            }
        }

        // Atualiza idade/tempo e publica status a cada segundo.
        scope.launch {
            while (isActive) {
                delay(1000)
                synchronized(lock) {
                    updateTimersAndStale()
                    publish(force = true)
                }
            }
        }

        // BUG 01: checkpoint crítico. Perda máxima normal ~5 s se o processo morrer de repente.
        scope.launch(Dispatchers.IO) {
            while (isActive) {
                delay(5000)
                synchronized(lock) {
                    updateSessionClock()
                    sessionStore.save(working)
                }
            }
        }
    }

    fun stop() {
        synchronized(lock) {
            updateSessionClock()
            sessionStore.save(working)
        }
        client.stop()
        scope.cancel()
    }

    fun resetFieldSession() {
        scope.launch(Dispatchers.IO) {
            // Na arquitetura Core-first, o reset precisa acontecer no ESP32 também.
            coreStateClient.resetSession()
            synchronized(lock) {
                sessionBaseSeconds = 0L
                sessionStart = SystemClock.elapsedRealtime()
                lastAnalyticsTick = sessionStart
                working = decoder.resetSession(working).copy(
                    connection = working.connection.copy(sessionSeconds = 0L),
                )
                sessionStore.clear()
                sessionStore.save(working)
                publish(force = true)
            }
        }
    }

    fun saveOccurrence(tag: String = "manual") {
        scope.launch(Dispatchers.IO) {
            try {
                val file = rawBuffer.save(context, tag)
                synchronized(lock) {
                    working = working.copy(logger = working.logger.copy(lastSavedFile = file.absolutePath))
                    publish(force = true)
                }
            } catch (t: Throwable) {
                synchronized(lock) {
                    working = working.copy(connection = working.connection.copy(lastError = "Falha ao salvar log: ${t.message}"))
                    publish(force = true)
                }
            }
        }
    }

    private fun onConnection(wifiBound: Boolean, tcpConnected: Boolean, error: String?) {
        synchronized(lock) {
            if (tcpConnected && !wasTcp) reconnects++
            wasTcp = tcpConnected
            working = working.copy(connection = working.connection.copy(
                wifiBound = wifiBound,
                tcpConnected = tcpConnected,
                reconnects = reconnects,
                lastError = error,
            ))
            publish(force = true)
        }
    }

    private fun onRawLine(line: String) {
        rawBuffer.add(line)
        val frame = Tb1LineParser.parse(line)
        val now = SystemClock.elapsedRealtime()
        synchronized(lock) {
            if (frame == null) {
                invalidLines++
                working = working.copy(connection = working.connection.copy(invalidLines = invalidLines))
                publish()
                return
            }

            totalFrames++
            fpsWindowFrames++
            lastFrameReceivedAt = now
            pgnCounts[frame.pgn] = (pgnCounts[frame.pgn] ?: 0L) + 1L
            recent.addFirst(
                RawFrameSummary(
                    frame.timestampMs,
                    frame.pgn,
                    frame.sa,
                    "%08X".format(frame.canId),
                    frame.data.joinToString("") { "%02X".format(it.toInt() and 0xFF) },
                )
            )
            while (recent.size > 10) recent.removeLast()

            val elapsedFps = now - fpsWindowStart
            if (elapsedFps >= 1000L) {
                fps = fpsWindowFrames * 1000.0 / elapsedFps
                fpsWindowFrames = 0L
                fpsWindowStart = now
            }

            working = decoder.decode(frame, working, now)
            updateAnalytics(now)
            updateSessionClock(now)
            working = working.copy(
                connection = working.connection.copy(
                    canActive = true,
                    framesPerSecond = fps,
                    totalFrames = totalFrames,
                    invalidLines = invalidLines,
                    lastFrameAgeMs = 0L,
                    sessionSeconds = currentSessionSeconds(now),
                ),
                logger = working.logger.copy(
                    pgnCounts = pgnCounts.toMap(),
                    recentFrames = recent.toList(),
                    ringBufferFrames = rawBuffer.size(),
                ),
            )
            publish()
        }
    }

    private fun currentSessionSeconds(now: Long = SystemClock.elapsedRealtime()): Long =
        sessionBaseSeconds + ((now - sessionStart).coerceAtLeast(0L) / 1000L)

    private fun updateSessionClock(now: Long = SystemClock.elapsedRealtime()) {
        working = working.copy(connection = working.connection.copy(sessionSeconds = currentSessionSeconds(now)))
    }

    private fun updateAnalytics(now: Long) {
        if (now - lastAnalyticsTick < 1000L) return
        val ticks = ((now - lastAnalyticsTick) / 1000L).coerceAtMost(5L)
        lastAnalyticsTick += ticks * 1000L
        val speed = working.speedKmh.value ?: 0.0
        val rpm = working.engine.rpm.value ?: 0.0
        val pedal = working.engine.acceleratorPct.value ?: 0.0
        val torque = working.engine.actualTorquePct.value ?: 0.0
        val bogieKg = working.weights.driveBogieKg.value ?: 0.0
        val combinationKg = working.weights.combinationKg.value ?: 0.0
        val clutchSlip = working.transmission.clutchSlipPct.value ?: 0.0
        val gear = working.transmission.currentGear
        val a = working.analytics
        val engineOn = rpm > 400.0
        val moving = speed > 3.0

        // Faixas provisórias do Eco Score v2 para teste de campo. A intenção é
        // calibrá-las com os logs reais do FH, não tratá-las como limites Volvo.
        val efficientRpm = moving && rpm in 900.0..1600.0
        val economicSpeed = moving && speed in 55.0..85.0
        val highPedal = moving && pedal > 90.0
        val justifiedHighDemand = highPedal && (torque >= 70.0 || bogieKg >= 15000.0 || combinationKg >= 40000.0)
        val smartTransmission = moving && gear != null && gear > 0 && rpm in 900.0..1600.0 && clutchSlip < 8.0
        val vebActive = moving && working.transmission.vebStageRaw > 0

        working = working.copy(analytics = a.copy(
            movingSeconds = a.movingSeconds + if (moving) ticks else 0,
            engineOnSeconds = a.engineOnSeconds + if (engineOn) ticks else 0,
            iRollSeconds = a.iRollSeconds + if (moving && working.transmission.iRollActive) ticks else 0,
            cruiseSeconds = a.cruiseSeconds + if (moving && working.transmission.cruiseActive) ticks else 0,
            idleSeconds = a.idleSeconds + if (engineOn && speed < 1.0) ticks else 0,
            highPedalSeconds = a.highPedalSeconds + if (highPedal) ticks else 0,
            efficientRpmSeconds = a.efficientRpmSeconds + if (efficientRpm) ticks else 0,
            economicSpeedSeconds = a.economicSpeedSeconds + if (economicSpeed) ticks else 0,
            justifiedHighDemandSeconds = a.justifiedHighDemandSeconds + if (justifiedHighDemand) ticks else 0,
            smartTransmissionSeconds = a.smartTransmissionSeconds + if (smartTransmission) ticks else 0,
            vebSeconds = a.vebSeconds + if (vebActive) ticks else 0,
        ))

        // Health Engine: roda no máximo 1 Hz, usa os mesmos sinais normalizados da UI
        // e persiste baselines/índices. Não afirma desgaste físico; calcula saúde/estresse/tendência.
        working = working.copy(health = healthEngine.update(working, now, ticks))
    }

    private fun updateTimersAndStale() {
        val now = SystemClock.elapsedRealtime()
        val age = if (lastFrameReceivedAt > 0L) now - lastFrameReceivedAt else null
        val can = age != null && age <= 2000L
        val e = working.engine
        val t = working.transmission
        working = working.copy(
            speedKmh = working.speedKmh.markStale(now, 1800L),
            odometerKm = working.odometerKm.markStale(now, 5000L),
            engine = e.copy(
                rpm = e.rpm.markStale(now, 1800L),
                acceleratorPct = e.acceleratorPct.markStale(now, 1800L),
                actualTorquePct = e.actualTorquePct.markStale(now, 1800L),
                powerKw = e.powerKw.markStale(now, 1800L),
                coolantTempC = e.coolantTempC.markStale(now, 3500L),
                oilTempC = e.oilTempC.markStale(now, 3500L),
                intakeAirTempC = e.intakeAirTempC.markStale(now, 3500L),
                oilPressureBar = e.oilPressureBar.markStale(now, 3500L),
                oilLevelPct = e.oilLevelPct.markStale(now, 5000L),
                fuelPressureBar = e.fuelPressureBar.markStale(now, 3500L),
                boostBar = e.boostBar.markStale(now, 2000L),
                ambientTempC = e.ambientTempC.markStale(now, 3500L),
            ),
            transmission = t.copy(
                inputShaftRpm = t.inputShaftRpm.markStale(now, 2000L),
                outputShaftRpm = t.outputShaftRpm.markStale(now, 2000L),
                clutchSlipPct = t.clutchSlipPct.markStale(now, 2000L),
                oilTempC = t.oilTempC.markStale(now, 3500L),
            ),
            weights = working.weights.copy(
                driveBogieKg = working.weights.driveBogieKg.markStale(now, 5000L),
                combinationKg = working.weights.combinationKg.markStale(now, 5000L),
            ),
            fuel = working.fuel.copy(fuelRateLph = working.fuel.fuelRateLph.markStale(now, 1800L)),
            gps = if (working.gps.hasFix && now - working.gps.receivedAtMs > 5000L) working.gps.copy(hasFix = false) else working.gps,
            connection = working.connection.copy(
                canActive = can,
                framesPerSecond = if (can) fps else 0.0,
                lastFrameAgeMs = age,
                totalFrames = totalFrames,
                invalidLines = invalidLines,
                reconnects = reconnects,
                sessionSeconds = currentSessionSeconds(now),
            ),
            logger = working.logger.copy(
                pgnCounts = pgnCounts.toMap(),
                recentFrames = recent.toList(),
                ringBufferFrames = rawBuffer.size(),
            ),
        )
    }

    private fun publish(force: Boolean = false) {
        val now = SystemClock.elapsedRealtime()
        if (!force && now - lastPublishAt < 100L) return
        lastPublishAt = now
        _state.value = working
    }
}
