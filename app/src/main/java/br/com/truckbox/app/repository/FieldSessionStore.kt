package br.com.truckbox.app.repository

import android.content.Context
import br.com.truckbox.app.data.*
import br.com.truckbox.app.domain.health.HealthLearningState

/**
 * Checkpoint leve do Field Test. Salva os acumuladores que NÃO podem zerar
 * quando o caminhão/tablet/app desligar. A gravação é periódica no repositório.
 */
class FieldSessionStore(context: Context) {
    private val p = context.getSharedPreferences("truckbox_field_session_v1", Context.MODE_PRIVATE)

    data class Snapshot(
        val sessionLiters: Double = 0.0,
        val sessionDistanceKm: Double = 0.0,
        val sessionSeconds: Long = 0L,
        val fuelReady: Boolean = false,
        val distanceReady: Boolean = false,
        val last50KmAverageKml: Double? = null,
        val best50KmAverageKml: Double? = null,
        val last100KmAverageKml: Double? = null,
        val lastOdometerKm: Double? = null,
        val operationMode: OperationMode = OperationMode.NO_TRIP,
        val activeTripId: String? = null,
        val analytics: DrivingAnalyticsState = DrivingAnalyticsState(),
        val healthLearning: HealthLearningState = HealthLearningState(),
    )

    fun load(): Snapshot = Snapshot(
        sessionLiters = getDouble("session_liters") ?: 0.0,
        sessionDistanceKm = getDouble("session_distance_km") ?: 0.0,
        sessionSeconds = p.getLong("session_seconds", 0L),
        fuelReady = p.getBoolean("fuel_ready", false),
        distanceReady = p.getBoolean("distance_ready", false),
        last50KmAverageKml = getDouble("last_50_kml"),
        best50KmAverageKml = getDouble("best_50_kml"),
        last100KmAverageKml = getDouble("last_100_kml"),
        lastOdometerKm = getDouble("last_odometer_km"),
        operationMode = runCatching {
            OperationMode.valueOf(p.getString("operation_mode", OperationMode.NO_TRIP.name)!!)
        }.getOrDefault(OperationMode.NO_TRIP),
        activeTripId = p.getString("active_trip_id", null),
        analytics = DrivingAnalyticsState(
            movingSeconds = p.getLong("moving_seconds", 0L),
            engineOnSeconds = p.getLong("engine_on_seconds", 0L),
            iRollSeconds = p.getLong("iroll_seconds", 0L),
            cruiseSeconds = p.getLong("cruise_seconds", 0L),
            idleSeconds = p.getLong("idle_seconds", 0L),
            highPedalSeconds = p.getLong("high_pedal_seconds", 0L),
            efficientRpmSeconds = p.getLong("efficient_rpm_seconds", 0L),
            economicSpeedSeconds = p.getLong("economic_speed_seconds", 0L),
            justifiedHighDemandSeconds = p.getLong("justified_high_demand_seconds", 0L),
            smartTransmissionSeconds = p.getLong("smart_transmission_seconds", 0L),
            vebSeconds = p.getLong("veb_seconds", 0L),
        ),
        healthLearning = HealthLearningState(
            observedSeconds = p.getLong("health_observed_seconds", 0L),
            oilPressureRatioBaseline = getDouble("health_oil_pressure_ratio_baseline"),
            oilPressureBaselineSamples = p.getLong("health_oil_pressure_samples", 0L),
            boostPerTorqueBaseline = getDouble("health_boost_per_torque_baseline"),
            boostBaselineSamples = p.getLong("health_boost_samples", 0L),
            fuelPressureBaselineBar = getDouble("health_fuel_pressure_baseline"),
            fuelPressureBaselineSamples = p.getLong("health_fuel_pressure_samples", 0L),
            shiftCount = p.getLong("health_shift_count", 0L),
            slowShiftCount = p.getLong("health_slow_shift_count", 0L),
            shiftDurationEwmaSec = getDouble("health_shift_duration_ewma"),
            shiftPeakSlipEwmaPct = getDouble("health_shift_slip_ewma"),
            huntingEvents = p.getLong("health_hunting_events", 0L),
            engineStressIntegral = getDouble("health_engine_stress_integral") ?: 0.0,
            engineSevereSeconds = p.getLong("health_engine_severe_seconds", 0L),
            transmissionStressIntegral = getDouble("health_trans_stress_integral") ?: 0.0,
            drivetrainStressIntegral = getDouble("health_drivetrain_stress_integral") ?: 0.0,
        ),
    )

    fun save(state: TruckState) {
        val e = p.edit()
        putDouble(e, "session_liters", state.fuel.sessionLiters)
        putDouble(e, "session_distance_km", state.fuel.sessionDistanceKm)
        putDouble(e, "last_50_kml", state.fuel.last50KmAverageKml)
        putDouble(e, "best_50_kml", state.fuel.best50KmAverageKml)
        putDouble(e, "last_100_kml", state.fuel.last100KmAverageKml)
        putDouble(e, "last_odometer_km", state.odometerKm.value)
        e.putLong("session_seconds", state.connection.sessionSeconds)
        e.putBoolean("fuel_ready", state.fuel.fuelReady)
        e.putBoolean("distance_ready", state.fuel.distanceReady)
        e.putString("operation_mode", state.operation.mode.name)
        e.putString("active_trip_id", state.operation.activeTripId)
        e.putLong("moving_seconds", state.analytics.movingSeconds)
        e.putLong("engine_on_seconds", state.analytics.engineOnSeconds)
        e.putLong("iroll_seconds", state.analytics.iRollSeconds)
        e.putLong("cruise_seconds", state.analytics.cruiseSeconds)
        e.putLong("idle_seconds", state.analytics.idleSeconds)
        e.putLong("high_pedal_seconds", state.analytics.highPedalSeconds)
        e.putLong("efficient_rpm_seconds", state.analytics.efficientRpmSeconds)
        e.putLong("economic_speed_seconds", state.analytics.economicSpeedSeconds)
        e.putLong("justified_high_demand_seconds", state.analytics.justifiedHighDemandSeconds)
        e.putLong("smart_transmission_seconds", state.analytics.smartTransmissionSeconds)
        e.putLong("veb_seconds", state.analytics.vebSeconds)

        val h = state.health.learning
        e.putLong("health_observed_seconds", h.observedSeconds)
        putDouble(e, "health_oil_pressure_ratio_baseline", h.oilPressureRatioBaseline)
        e.putLong("health_oil_pressure_samples", h.oilPressureBaselineSamples)
        putDouble(e, "health_boost_per_torque_baseline", h.boostPerTorqueBaseline)
        e.putLong("health_boost_samples", h.boostBaselineSamples)
        putDouble(e, "health_fuel_pressure_baseline", h.fuelPressureBaselineBar)
        e.putLong("health_fuel_pressure_samples", h.fuelPressureBaselineSamples)
        e.putLong("health_shift_count", h.shiftCount)
        e.putLong("health_slow_shift_count", h.slowShiftCount)
        putDouble(e, "health_shift_duration_ewma", h.shiftDurationEwmaSec)
        putDouble(e, "health_shift_slip_ewma", h.shiftPeakSlipEwmaPct)
        e.putLong("health_hunting_events", h.huntingEvents)
        putDouble(e, "health_engine_stress_integral", h.engineStressIntegral)
        e.putLong("health_engine_severe_seconds", h.engineSevereSeconds)
        putDouble(e, "health_trans_stress_integral", h.transmissionStressIntegral)
        putDouble(e, "health_drivetrain_stress_integral", h.drivetrainStressIntegral)
        e.apply()
    }

    fun clear() = p.edit().clear().apply()

    private fun getDouble(key: String): Double? =
        if (!p.contains(key)) null else Double.fromBits(p.getLong(key, 0L))

    private fun putDouble(editor: android.content.SharedPreferences.Editor, key: String, value: Double?) {
        if (value == null) editor.remove(key) else editor.putLong(key, value.toRawBits())
    }
}
