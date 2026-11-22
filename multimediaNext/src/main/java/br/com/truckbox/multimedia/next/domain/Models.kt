package br.com.truckbox.multimedia.next.domain

import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime

enum class Quality { VALID, PROVISIONAL, STALE, UNVERIFIED, MISSING, INVALID }
enum class Origin { CORE, CLOUD, DEMO }

data class Reading(
    val value: Double? = null,
    val unit: String,
    val source: Origin,
    val sampledAt: Instant? = null,
    val receivedAt: Instant? = null,
    val quality: Quality = Quality.MISSING,
    val sourceId: String? = null,
    val captureId: Long? = null,
    val bootId: Long? = null,
    val validation: String? = null,
) {
    fun current(now: Instant, maxAgeSeconds: Long = 5): Double? {
        if(quality!=Quality.VALID) return null
        return freshValue(now,maxAgeSeconds)
    }
    /** Observação real com metadados, ainda em validação; nunca autoriza cálculos ou ações. */
    fun observed(now: Instant, maxAgeSeconds: Long = 5): Double? {
        if(quality!=Quality.VALID && quality!=Quality.PROVISIONAL) return null
        return freshValue(now,maxAgeSeconds)
    }
    private fun freshValue(now: Instant, maxAgeSeconds: Long): Double? {
        val at = sampledAt ?: return null
        if (sourceId.isNullOrBlank()) return null
        if (at.isAfter(now) || java.time.Duration.between(at,now).toMillis()>maxAgeSeconds*1000) return null
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
data class CoreInternetSharing(val naptActive: Boolean,val status: String,val observedAt: Instant) {
    fun label(now: Instant): String {
        if(java.time.Duration.between(observedAt,now).seconds !in 0..10)return "Estado do compartilhamento desatualizado"
        return when(status) {
            "NAPT_ACTIVE_NOT_VERIFIED" -> if(naptActive) "Encaminhamento ativo · Internet não verificada" else "Não verificado"
            "NO_UPLINK_CONFIGURED" -> "Wi-Fi externo não configurado no Core"
            "WAITING_UPLINK","UPLINK_DISCONNECTED","UPLINK_IP_LOST","NO_UPSTREAM_GATEWAY" -> "Core sem acesso à rede externa"
            "SUBNET_CONFLICT" -> "Redes do Core e do roteador em conflito"
            "INSUFFICIENT_HEAP" -> "Compartilhamento suspenso · memória insuficiente"
            "DHCP_DNS_ERROR" -> "DNS da rede externa indisponível"
            else -> "Compartilhamento não verificado"
        }
    }
}
data class CloudPosition(val latitude: Double,val longitude: Double,val sampledAt: Instant,val source: String,val provider: String?,val accuracyM: Double?)
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
enum class FuelQuality { INTEGRATED_PID183, RECONCILED_PID250, ESTIMATED_GAPS, INSUFFICIENT }
data class FuelMeasurement(val quality: FuelQuality, val sampledAt: Instant?) {
    fun label(now: Instant): String = when {
        quality == FuelQuality.INSUFFICIENT -> "Cobertura insuficiente"
        sampledAt == null || sampledAt.isAfter(now.plusSeconds(5)) || sampledAt.isBefore(now.minusSeconds(35)) -> "Leitura desatualizada"
        quality == FuelQuality.RECONCILED_PID250 -> "Conferido pelo PID 250"
        quality == FuelQuality.ESTIMATED_GAPS -> "Estimado · lacunas de leitura"
        else -> "Estimado pelo PID 183"
    }
}
data class LocalTripFuel(val distanceKm: Double?, val litres: Double, val gaps: Boolean) {
    val averageKml get() = distanceKm?.let { if (litres > 0) it / litres else null }
    val label get() = if (gaps) "Estimativa local · lacunas" else "Estimativa local pelo PID 183"
}
data class LocalFuelWindow(val coreUid: String, val endedAt: Instant, val coveredMs: Long, val microlitres: Long,
                           val trip: LocalTripFuel? = null) {
    fun fresh(now: Instant) = !endedAt.isAfter(now) && !endedAt.isBefore(now.minusSeconds(30))
    val averageLph: Double? get() = if (coveredMs == 10000L) microlitres * 0.00036 else null
}
data class LegConsumption(val legId: String, val kind: LegKind, val distanceKm: Double, val consumedLiters: Double,
                          val fuel: FuelMeasurement? = null) {
    val averageKml: Double? get() = if (distanceKm >= 0 && consumedLiters > 0) distanceKm / consumedLiters else null
}
data class TripView(
    val id: String, val legId: String, val origin: String, val destination: String,
    val kind: LegKind, val consumption: LegConsumption?,
    val product: String? = null, val weightT: Double? = null,
    val remainingKm: Double? = null, val legStartedAt: Instant? = null,
)
enum class ConnectionState(val label: String) {
    UNVERIFIED("Não verificado"), HTTP_RESPONDED("HTTP respondeu · sinais não verificados"),
    SENSORS_RECEIVED("Leituras recebidas · conferir qualidade"),
    CLOUD_AUTHENTICATED("Resposta autenticada da Cloud"), AUTH_REQUIRED("Acesso à Cloud necessário"),
    AUTH_PENDING("Aguardando vínculo na Cloud"),
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
    val coreDeviceUid: String? = null,
    val coreInternetSharing: CoreInternetSharing? = null,
    val coreDiagnostics: CoreDiagnostics? = null,
    // Evidência real de PID194; registros conservados, ausência de frame não encerra DTC.
    val coreFaults: List<EcuFault> = emptyList(),
    val faultUid: String? = null,
    val faultSnapshotAt: Instant? = null,
    val faultReadable: Boolean = false,
    val faultIncomplete: Boolean = true,
    val faultOverflow: Int = 0,
    val speed: Reading? = null,
    val fuelRate: Reading? = null,
    val localFuel: LocalFuelWindow? = null,
    val trip: TripView? = null,
    val gps: CloudPosition? = null,
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
    val policyExpiresAt: Instant? = null,
    val cloudScope: IdentityScope? = null,
    val vehicleLabel: String? = null,
    val expectedCoreUid: String? = null,
    val lastCloudResponse: Instant? = null,
    val series: Map<Instrument, List<TimeSample>> = emptyMap(),
) {
    // Defesa final antes da composição, inclusive contra retorno tardio de requests.
    fun redacted(now: Instant=Instant.now()): AppState {
        val lease=demo || policyExpiresAt?.isAfter(now)==true
        val f=financialAllowed && lease;val j=journeyAllowed && lease
        return copy(financial=financial.takeIf {f},journey=journey.takeIf {j},financialAllowed=f,journeyAllowed=j,
            policyLabel=if(!lease && policyVersion!=null) "Sem sincronização recente da Cloud" else policyLabel)
    }
}
data class FinancialView(val contractedCents: Long?, val incurredCents: Long, val projectedCents: Long)
data class JourneyView(val reference: Instant, val summary: JourneySummary)

fun canEditDashboard(speed: Reading?, now: Instant, cloudAuthorized: Boolean): Boolean =
    cloudAuthorized && speed?.current(now) == 0.0
