package br.com.truckbox.multimedia.next.domain

import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime

enum class Quality { VALID, STALE, UNVERIFIED, MISSING, INVALID }
enum class Origin { CORE, CLOUD, DEMO }

data class Reading(
    val value: Double? = null,
    val unit: String,
    val source: Origin,
    val sampledAt: Instant? = null,
    val receivedAt: Instant? = null,
    val quality: Quality = Quality.MISSING,
    val sourceId: String? = null,
) {
    fun current(now: Instant, maxAgeSeconds: Long = 5): Double? {
        val at = sampledAt ?: return null
        if (quality != Quality.VALID || sourceId.isNullOrBlank()) return null
        if (at.isAfter(now) || now.epochSecond - at.epochSecond > maxAgeSeconds) return null
        return value?.takeIf { it.isFinite() }
    }
}

// Ordem contratual: não ordenar alfabeticamente e não dividir AIR em dois cartões.
enum class Instrument(val label: String, val unit: String, val coreKey: String?) {
    OIL_PRESSURE("Pressão do óleo motor", "bar", "oilPressureBar"),
    BOOST("Pressão do turbo", "bar", "boostBar"),
    FUEL_PRESSURE("Pressão do combustível", "bar", "fuelPressureBar"),
    COOLANT("Temperatura da água", "°C", "coolantTempC"),
    OIL_TEMP("Temperatura do óleo motor", "°C", "oilTempC"),
    INTAKE("Temperatura do ar de admissão", "°C", "intakeAirTempC"),
    GEARBOX("Temperatura do óleo i-Shift", "°C", "gearboxTempC"),
    GEAR("Marcha atual", "", "currentGear"),
    VOLTAGE("Tensão elétrica", "V", null),
    AMBIENT("Temperatura ambiente", "°C", "ambientTempC"),
    PEDAL("Pedal do acelerador", "%", "acceleratorPct"),
    AIR("Pressão de ar · circuitos 1 / 2", "bar", null),
    TORQUE("Torque do motor", "%", "actualTorquePct");

    companion object { val defaults = entries.toList() }
}

data class InstrumentValue(val first: Reading, val second: Reading? = null)
enum class Page(val title: String) {
    HOME("Home"), CONSUMPTION("Consumo"), TRIP("Viagem atual"),
    TRUCK("Caminhão e Diagnóstico"), SETTINGS("Configurações")
}
enum class TruckSystem(val title: String, val sensors: List<Instrument>) {
    GENERAL("Geral", Instrument.defaults),
    ENGINE("Motor", listOf(Instrument.OIL_PRESSURE, Instrument.COOLANT, Instrument.OIL_TEMP, Instrument.TORQUE, Instrument.FUEL_PRESSURE)),
    INTAKE("Admissão/Turbo", listOf(Instrument.BOOST, Instrument.INTAKE, Instrument.AMBIENT, Instrument.TORQUE)),
    TRANSMISSION("Transmissão i-Shift", listOf(Instrument.GEAR, Instrument.GEARBOX)),
    BRAKES("Freios/Ar", listOf(Instrument.AIR)),
    ELECTRICAL("Elétrico", listOf(Instrument.VOLTAGE));
}
enum class AnalysisPeriod(val title: String, val seconds: Long?) {
    MINUTES_15("15 min", 900), HOUR("1 hora", 3600), TRIP("Viagem", null)
}
enum class LegKind(val title: String) { COMPLETE("Viagem completa"), LOADED("Trecho carregado"), EMPTY("Trecho vazio") }
data class LegConsumption(val legId: String, val kind: LegKind, val distanceKm: Double, val consumedLiters: Double) {
    val averageKml: Double? get() = if (distanceKm >= 0 && consumedLiters > 0) distanceKm / consumedLiters else null
}
data class TripView(
    val id: String, val legId: String, val origin: String, val destination: String,
    val kind: LegKind, val consumption: LegConsumption,
    val product: String? = null, val weightT: Double? = null,
    val remainingKm: Double? = null, val legStartedAt: Instant? = null,
)
enum class ConnectionState(val label: String) {
    UNVERIFIED("Não verificado"), HTTP_RESPONDED("HTTP respondeu · sinais não verificados"),
    OFFLINE("Sem resposta"), BLOCKED("Aguardando contrato seguro"), DEMO("Simulado")
}
enum class Appearance { AUTO, NIGHT, DAY }
data class LocalPreferences(val appearance: Appearance = Appearance.AUTO, val coreHost: String = "truckbox.local")
fun isNight(at: Instant, appearance: Appearance): Boolean = when (appearance) {
    Appearance.NIGHT -> true
    Appearance.DAY -> false
    Appearance.AUTO -> ZonedDateTime.ofInstant(at, ZoneId.of("America/Sao_Paulo")).hour.let { it >= 18 || it < 6 }
}

data class AppState(
    val demo: Boolean = false,
    val readings: Map<Instrument, InstrumentValue> = emptyMap(),
    val core: ConnectionState = ConnectionState.UNVERIFIED,
    val cloud: ConnectionState = ConnectionState.BLOCKED,
    val lastCoreResponse: Instant? = null,
    val speed: Reading? = null,
    val fuelRate: Reading? = null,
    val trip: TripView? = null,
    val legs: List<LegConsumption> = emptyList(),
    val last100Kml: Double? = null,
    val tankLiters: Double? = null,
    val tankStatus: String = "Calibre em tanque cheio",
    val financial: FinancialView? = null,
    val journey: JourneyView? = null,
    val financialAllowed: Boolean = false,
    val journeyAllowed: Boolean = false,
    val policyLabel: String = "Aguardando sincronização segura",
    val policyVersion: Long? = null,
    val series: Map<Instrument, List<TimeSample>> = emptyMap(),
) {
    // Defesa final antes da composição, inclusive contra retorno tardio de requests.
    fun redacted() = copy(financial = financial.takeIf { financialAllowed }, journey = journey.takeIf { journeyAllowed })
}
data class FinancialView(val contractedCents: Long?, val incurredCents: Long, val projectedCents: Long)
data class JourneyView(val reference: Instant, val summary: JourneySummary)

fun canEditDashboard(speed: Reading?, now: Instant, cloudAuthorized: Boolean): Boolean =
    cloudAuthorized && speed?.current(now) == 0.0
