package br.com.truckbox.app.ui.screens

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.app.data.DataQuality
import br.com.truckbox.app.data.SensorValue
import br.com.truckbox.app.data.TruckState
import br.com.truckbox.app.domain.health.HealthConfidence
import br.com.truckbox.app.domain.health.HealthItem
import br.com.truckbox.app.domain.health.HealthSeverity
import br.com.truckbox.app.ui.theme.*
import java.util.Locale

private enum class SensorGroupUi(val label: String) {
    ALL("Todos"), ENGINE("Motor"), TRANSMISSION("Transmissão"), VEHICLE("Veículo"), FUEL("Combustível"), GPS("GPS"), CALCULATED("Calculados")
}

private data class SensorUi(
    val group: SensorGroupUi,
    val name: String,
    val value: String,
    val unit: String = "",
    val meta: String,
    val quality: DataQuality = DataQuality.VALID,
    val calculated: Boolean = false,
)

/**
 * Catálogo vivo de todos os sinais que a v0.4.2 já usa.
 * Mostra os sinais recebidos do J1939 e os cálculos TruckBox na mesma tela.
 */
@Composable
fun SensorsScreen(state: TruckState) {
    var selectedGroup by remember { mutableStateOf(SensorGroupUi.ALL) }
    var selectedHealth by remember { mutableStateOf<HealthItem?>(null) }
    val sensors = remember(state) { sensorCatalog(state) }
    val filtered = if (selectedGroup == SensorGroupUi.ALL) sensors else sensors.filter { it.group == selectedGroup }

    Column(Modifier.fillMaxSize()) {
        TruckHeader(state)
        Column(
            Modifier.fillMaxSize().padding(horizontal = 10.dp, vertical = 7.dp),
            verticalArrangement = Arrangement.spacedBy(7.dp),
        ) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Default.Sensors, null, tint = MaterialTheme.colorScheme.primary)
                Spacer(Modifier.width(7.dp))
                Text("SENSORES, CÁLCULOS E SAÚDE", fontWeight = FontWeight.Black, fontSize = 18.sp)
                Spacer(Modifier.weight(1f))
                Text("${sensors.count { it.quality == DataQuality.VALID }} ativos • ${sensors.size} mapeados/cálculos", color = TruckMuted, fontSize = 10.sp)
            }

            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SensorGroupUi.entries.forEach { group ->
                    FilterChip(
                        selected = selectedGroup == group,
                        onClick = { selectedGroup = group },
                        label = { Text(group.label, fontSize = 10.sp) },
                    )
                }
            }

            Row(Modifier.fillMaxSize(), horizontalArrangement = Arrangement.spacedBy(9.dp)) {
                TruckCard(Modifier.weight(0.70f).fillMaxHeight()) {
                    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                        if (selectedGroup == SensorGroupUi.ALL) {
                            SensorGroupUi.entries.filter { it != SensorGroupUi.ALL }.forEach { group ->
                                val groupItems = sensors.filter { it.group == group }
                                if (groupItems.isNotEmpty()) {
                                    GroupHeader(group.label, groupItems.size)
                                    SensorGrid(groupItems)
                                    Spacer(Modifier.height(7.dp))
                                }
                            }
                        } else {
                            GroupHeader(selectedGroup.label, filtered.size)
                            SensorGrid(filtered)
                        }
                    }
                }

                Column(Modifier.weight(0.30f).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    StressSummary(state)
                    TruckCard(Modifier.fillMaxWidth().weight(1f)) {
                        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                            Text("SAÚDE E STATUS", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Black, fontSize = 13.sp)
                            Spacer(Modifier.weight(1f))
                            Text("${state.health.items.size}", color = TruckMuted, fontSize = 10.sp)
                        }
                        Spacer(Modifier.height(6.dp))
                        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                            if (state.health.items.isEmpty()) {
                                Text("Aguardando dados para iniciar os cálculos.", color = TruckMuted, fontSize = 10.sp)
                            }
                            state.health.items.forEach { item ->
                                HealthCompactCard(item, onClick = { selectedHealth = item })
                                Spacer(Modifier.height(5.dp))
                            }
                        }
                    }
                }
            }
        }
    }

    selectedHealth?.let { item -> HealthDetailDialog(item, onDismiss = { selectedHealth = null }) }
}

@Composable
private fun StressSummary(state: TruckState) {
    TruckCard(Modifier.fillMaxWidth()) {
        Text("ÍNDICES DE ESTRESSE", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Black, fontSize = 12.sp)
        Spacer(Modifier.height(6.dp))
        StressLine("Motor", state.health.engineCurrentStress, state.health.engineAverageStress)
        StressLine("Transmissão", state.health.transmissionCurrentStress, state.health.transmissionAverageStress)
        StressLine("Trem de força", state.health.drivetrainCurrentStress, state.health.drivetrainAverageStress)
        Spacer(Modifier.height(4.dp))
        Text("Não é % de desgaste físico. É um índice de esforço/condição calculado.", color = TruckMuted, fontSize = 8.sp)
    }
}

@Composable
private fun StressLine(label: String, current: Int, average: Int) {
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.width(86.dp), fontSize = 9.sp, fontWeight = FontWeight.Bold)
        LinearProgressIndicator(
            progress = { current.coerceIn(0,100) / 100f },
            modifier = Modifier.weight(1f).height(5.dp),
            color = when { current >= 90 -> StatusCritical; current >= 75 -> StatusWarn; else -> StatusOk },
            trackColor = TruckSurface3,
        )
        Spacer(Modifier.width(6.dp))
        Text("$current", fontWeight = FontWeight.Black, fontSize = 10.sp, color = when { current >= 90 -> StatusCritical; current >= 75 -> StatusWarn; else -> TruckText })
        Text(" / méd. $average", color = TruckMuted, fontSize = 8.sp)
    }
}

@Composable
private fun GroupHeader(label: String, count: Int) {
    Row(Modifier.fillMaxWidth().padding(bottom = 5.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label.uppercase(), color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Black, fontSize = 12.sp)
        Spacer(Modifier.width(7.dp))
        Text("$count itens", color = TruckMuted, fontSize = 9.sp)
    }
}

@Composable
private fun SensorGrid(items: List<SensorUi>) {
    items.chunked(4).forEach { rowItems ->
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            rowItems.forEach { sensor -> SensorTile(sensor, Modifier.weight(1f)) }
            repeat(4 - rowItems.size) { Spacer(Modifier.weight(1f)) }
        }
        Spacer(Modifier.height(6.dp))
    }
}

@Composable
private fun SensorTile(sensor: SensorUi, modifier: Modifier = Modifier) {
    val qColor = when (sensor.quality) {
        DataQuality.VALID -> StatusOk
        DataQuality.STALE -> StatusWarn
        DataQuality.SUSPECT -> StatusWarn
        DataQuality.ALIAS -> StatusInfo
        DataQuality.UNAVAILABLE -> TruckMuted
    }
    Surface(
        modifier = modifier.height(86.dp),
        shape = RoundedCornerShape(11.dp),
        color = TruckSurface2,
        border = BorderStroke(1.dp, if (sensor.calculated) MaterialTheme.colorScheme.primary.copy(alpha = 0.38f) else TruckBorder),
    ) {
        Column(Modifier.fillMaxSize().padding(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(sensor.name, fontWeight = FontWeight.Bold, fontSize = 9.sp, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                if (sensor.calculated) Text("CALC", color = MaterialTheme.colorScheme.primary, fontSize = 7.sp, fontWeight = FontWeight.Black)
            }
            Row(verticalAlignment = Alignment.Bottom) {
                Text(sensor.value, fontWeight = FontWeight.Black, fontSize = 19.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (sensor.unit.isNotBlank()) { Spacer(Modifier.width(3.dp)); Text(sensor.unit, color = TruckMuted, fontSize = 8.sp, modifier = Modifier.padding(bottom = 3.dp)) }
            }
            Spacer(Modifier.weight(1f))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(6.dp).background(qColor, RoundedCornerShape(50)))
                Spacer(Modifier.width(4.dp))
                Text(sensor.quality.name, color = qColor, fontSize = 7.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.weight(1f))
                Text(sensor.meta, color = TruckMuted, fontSize = 7.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
    }
}

@Composable
private fun HealthCompactCard(item: HealthItem, onClick: () -> Unit) {
    val color = healthColor(item.severity)
    Surface(
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick),
        shape = RoundedCornerShape(10.dp),
        color = color.copy(alpha = if (item.severity == HealthSeverity.OK) 0.055f else 0.09f),
        border = BorderStroke(1.dp, color.copy(alpha = 0.48f)),
    ) {
        Column(Modifier.padding(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(7.dp).background(color, RoundedCornerShape(50)))
                Spacer(Modifier.width(5.dp))
                Text(item.title, fontWeight = FontWeight.Black, fontSize = 9.sp, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                item.score?.let { Text("$it/100", color = color, fontWeight = FontWeight.Black, fontSize = 9.sp) }
            }
            Text(item.headline, color = color, fontWeight = FontWeight.Bold, fontSize = 9.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(item.detail, color = TruckMuted, fontSize = 7.5.sp, maxLines = 2, overflow = TextOverflow.Ellipsis)
        }
    }
}

@Composable
private fun HealthDetailDialog(item: HealthItem, onDismiss: () -> Unit) {
    val color = healthColor(item.severity)
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(item.title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(9.dp)) {
                Surface(shape = RoundedCornerShape(10.dp), color = color.copy(alpha = 0.10f), border = BorderStroke(1.dp, color.copy(alpha = 0.5f))) {
                    Column(Modifier.fillMaxWidth().padding(12.dp)) {
                        Text(item.severity.labelPt(), color = color, fontWeight = FontWeight.Black)
                        Text(item.headline, fontWeight = FontWeight.Black, fontSize = 18.sp)
                        Text(item.detail, color = TruckMuted, fontSize = 11.sp)
                    }
                }
                Text("Confiança: ${item.confidence.labelPt()}", fontWeight = FontWeight.Bold)
                Text("Base do cálculo", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                item.basedOn.forEach { Text("• $it", fontSize = 11.sp) }
                Text("O TruckBox calcula tendência/estresse. Não substitui os alertas OEM nem representa percentual físico de desgaste.", color = TruckMuted, fontSize = 9.sp)
            }
        },
        confirmButton = { Button(onClick = onDismiss) { Text("FECHAR") } },
    )
}

private fun healthColor(s: HealthSeverity): Color = when (s) {
    HealthSeverity.OK -> StatusOk
    HealthSeverity.INFO -> StatusInfo
    HealthSeverity.LEARNING -> MaterialThemeColorFallback
    HealthSeverity.WARNING -> StatusWarn
    HealthSeverity.CRITICAL -> StatusCritical
    HealthSeverity.UNAVAILABLE -> TruckMuted
}

// Fora de @Composable não podemos acessar MaterialTheme; usa azul TruckBox para aprendizado.
private val MaterialThemeColorFallback = StatusInfo
private fun HealthSeverity.labelPt(): String = when(this) {
    HealthSeverity.OK -> "OK"
    HealthSeverity.INFO -> "INFORMAÇÃO"
    HealthSeverity.LEARNING -> "EM APRENDIZADO"
    HealthSeverity.WARNING -> "ATENÇÃO"
    HealthSeverity.CRITICAL -> "CRÍTICO"
    HealthSeverity.UNAVAILABLE -> "SEM SINAL"
}
private fun HealthConfidence.labelPt(): String = when(this) { HealthConfidence.LOW -> "Baixa"; HealthConfidence.MEDIUM -> "Média"; HealthConfidence.HIGH -> "Alta" }

private fun sensorCatalog(s: TruckState): List<SensorUi> {
    fun num(group: SensorGroupUi, name: String, sv: SensorValue, unit: String, meta: String, dec: Int = 1) =
        SensorUi(group, name, sv.value?.let { fmt(it, dec) } ?: "—", unit, meta, sv.quality)
    fun calc(group: SensorGroupUi, name: String, value: Double?, unit: String, meta: String, dec: Int = 1) =
        SensorUi(group, name, value?.let { fmt(it, dec) } ?: "—", unit, meta, if (value != null) DataQuality.VALID else DataQuality.UNAVAILABLE, true)
    fun seen(pgn: Int): DataQuality = if (s.connection.canActive && (s.logger.pgnCounts[pgn] ?: 0L) > 0) DataQuality.VALID else DataQuality.UNAVAILABLE
    fun discrete(group: SensorGroupUi, name: String, value: String, pgn: Int, meta: String = "PGN $pgn") = SensorUi(group, name, value, "", meta, seen(pgn))

    val t = s.transmission
    val pred = t.prediction
    return listOf(
        num(SensorGroupUi.ENGINE, "RPM", s.engine.rpm, "rpm", "PGN 61444 • SPN 190", 0),
        num(SensorGroupUi.ENGINE, "Torque motor", s.engine.actualTorquePct, "%", "PGN 61444 • SPN 513", 0),
        calc(SensorGroupUi.CALCULATED, "Potência estimada", s.engine.powerKw.value, "kW", "RPM × torque", 0),
        num(SensorGroupUi.ENGINE, "Pedal acelerador", s.engine.acceleratorPct, "%", "PGN 61443 • SPN 91", 0),
        num(SensorGroupUi.ENGINE, "Temp. água", s.engine.coolantTempC, "°C", "PGN 65262 • SPN 110", 0),
        num(SensorGroupUi.ENGINE, "Temp. óleo motor", s.engine.oilTempC, "°C", "PGN 65262 • SPN 175", 0),
        num(SensorGroupUi.ENGINE, "Pressão óleo", s.engine.oilPressureBar, "bar", "PGN 65263 • SPN 100", 2),
        num(SensorGroupUi.ENGINE, "Nível óleo", s.engine.oilLevelPct, "%", "PGN 65263 • SPN 98", 0),
        num(SensorGroupUi.ENGINE, "Pressão combustível", s.engine.fuelPressureBar, "bar", "PGN 65263 • SPN 94", 2),
        num(SensorGroupUi.ENGINE, "Pressão turbo", s.engine.boostBar, "bar", "PGN 65270 • SPN 102", 2),
        num(SensorGroupUi.ENGINE, "Temp. admissão", s.engine.intakeAirTempC, "°C", "PGN 65270 • SPN 105", 0),
        num(SensorGroupUi.ENGINE, "Temp. ambiente", s.engine.ambientTempC, "°C", "PGN 65269 • SPN 171", 0),

        discrete(SensorGroupUi.TRANSMISSION, "Marcha atual", t.currentGear?.toString() ?: "—", 61445, "PGN 61445 • ETC2"),
        discrete(SensorGroupUi.TRANSMISSION, "Marcha selecionada", t.selectedGear?.toString() ?: "—", 61445, "PGN 61445 • ETC2"),
        SensorUi(SensorGroupUi.TRANSMISSION, "Relação da marcha", t.actualGearRatio?.let { fmt(it,3) } ?: "—", "", "PGN 61445", seen(61445)),
        num(SensorGroupUi.TRANSMISSION, "RPM entrada caixa", t.inputShaftRpm, "rpm", "PGN 61442", 0),
        num(SensorGroupUi.TRANSMISSION, "RPM saída caixa", t.outputShaftRpm, "rpm", "PGN 61442", 0),
        num(SensorGroupUi.TRANSMISSION, "Deslizamento embreagem", t.clutchSlipPct, "%", "PGN 61442", 1),
        num(SensorGroupUi.TRANSMISSION, "Temp. óleo caixa", t.oilTempC, "°C", "PGN 65272 • TRF1", 0),
        discrete(SensorGroupUi.TRANSMISSION, "I-Roll", if (t.iRollActive) "ATIVO" else "INATIVO", 65321, "PGN 65321 / FF29"),
        discrete(SensorGroupUi.TRANSMISSION, "Piloto automático", if (t.cruiseActive) "ATIVO" else "INATIVO", 65323, "PGN 65323 / FF2B"),
        discrete(SensorGroupUi.TRANSMISSION, "VEB", t.vebStageRaw.toString(), 65323, "PGN 65323 / FF2B"),
        discrete(SensorGroupUi.TRANSMISSION, "Modo I-Shift", t.ishiftMode ?: "—", 65286, "PGN 65286 / FF06"),
        SensorUi(SensorGroupUi.CALCULATED, "I-Shift Predict", pred.predictedGear?.let { "${t.currentGear ?: "—"}→$it" } ?: "—", "", "modelo TruckBox • score ${fmt(pred.score*100,0)}%", if (pred.predictedGear != null) DataQuality.VALID else DataQuality.UNAVAILABLE, true),

        num(SensorGroupUi.VEHICLE, "Velocidade CAN", s.speedKmh, "km/h", "PGN 65265 • SPN 84", 1),
        num(SensorGroupUi.VEHICLE, "Odômetro", s.odometerKm, "km", "PGN 65217 • VDHR", 1),
        num(SensorGroupUi.VEHICLE, "Peso bogie", s.weights.driveBogieKg, "kg", "PGN 65258 • VW", 0),
        num(SensorGroupUi.VEHICLE, "Peso conjunto", s.weights.combinationKg, "kg", "PGN 65295", 0),

        num(SensorGroupUi.FUEL, "Vazão estimada", s.fuel.fuelRateLph, "L/h", "derivado PGN 61444", 1),
        calc(SensorGroupUi.FUEL, "Consumido sessão", s.fuel.sessionLiters.takeIf { s.fuel.fuelReady }, "L", "acumulador TruckBox", 1),
        calc(SensorGroupUi.FUEL, "Distância sessão", s.fuel.sessionDistanceKm.takeIf { s.fuel.distanceReady }, "km", "PGN 65217", 1),
        calc(SensorGroupUi.FUEL, "Média sessão", s.fuel.sessionAverageKml, "km/L", "distância ÷ litros", 2),
        calc(SensorGroupUi.FUEL, "Média últimos 50 km", s.fuel.last50KmAverageKml, "km/L", "janela móvel", 2),
        calc(SensorGroupUi.FUEL, "Média últimos 100 km", s.fuel.last100KmAverageKml, "km/L", "janela móvel", 2),

        SensorUi(SensorGroupUi.GPS, "Velocidade GPS", s.gps.speedKmh?.let { fmt(it,1) } ?: "—", "km/h", "GPS externo / Core", if (s.gps.hasFix) DataQuality.VALID else DataQuality.UNAVAILABLE),
        SensorUi(SensorGroupUi.GPS, "Latitude", s.gps.latitude?.let { fmt(it,6) } ?: "—", "", "GPS externo / Core", if (s.gps.hasFix) DataQuality.VALID else DataQuality.UNAVAILABLE),
        SensorUi(SensorGroupUi.GPS, "Longitude", s.gps.longitude?.let { fmt(it,6) } ?: "—", "", "GPS externo / Core", if (s.gps.hasFix) DataQuality.VALID else DataQuality.UNAVAILABLE),
        SensorUi(SensorGroupUi.GPS, "Altitude", s.gps.altitudeM?.let { fmt(it,0) } ?: "—", "m", "GPS externo / Core", if (s.gps.hasFix) DataQuality.VALID else DataQuality.UNAVAILABLE),
        SensorUi(SensorGroupUi.GPS, "Precisão", s.gps.accuracyM?.let { fmt(it,0) } ?: "—", "m", "GPS externo / Core", if (s.gps.hasFix) DataQuality.VALID else DataQuality.UNAVAILABLE),
        SensorUi(SensorGroupUi.GPS, "Satélites", if (s.gps.hasFix) s.gps.satellites.toString() else "—", "", "GPS externo / Core", if (s.gps.hasFix) DataQuality.VALID else DataQuality.UNAVAILABLE),

        calc(SensorGroupUi.CALCULATED, "Estresse motor", s.health.engineCurrentStress.toDouble(), "/100", "Health Engine", 0),
        calc(SensorGroupUi.CALCULATED, "Estresse transmissão", s.health.transmissionCurrentStress.toDouble(), "/100", "Health Engine", 0),
        calc(SensorGroupUi.CALCULATED, "Estresse trem força", s.health.drivetrainCurrentStress.toDouble(), "/100", "Health Engine", 0),
    )
}

private fun fmt(v: Double, decimals: Int): String = String.format(Locale.US, "%.${decimals}f", v)
