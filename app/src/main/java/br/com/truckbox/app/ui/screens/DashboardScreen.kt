package br.com.truckbox.app.ui.screens

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.app.data.*
import br.com.truckbox.app.domain.efficiency.EcoEfficiency
import br.com.truckbox.app.ui.theme.*
import kotlinx.coroutines.delay
import kotlin.math.roundToInt

private data class SensorSlide(
    val title: String,
    val value: () -> Double?,
    val unit: String,
    val decimals: Int = 1,
)

@Composable
fun DashboardScreen(state: TruckState, onResetSession: () -> Unit = {}) {
    Column(Modifier.fillMaxSize()) {
        TruckHeader(state)
        BoxWithConstraints(Modifier.fillMaxWidth().weight(1f)) {
            val compact = maxHeight < 520.dp
            val gap = if (compact) 7.dp else 10.dp
            Row(
                Modifier.fillMaxSize().padding(horizontal = if (compact) 9.dp else 13.dp, vertical = if (compact) 7.dp else 10.dp),
                horizontalArrangement = Arrangement.spacedBy(gap),
            ) {
                Column(
                    Modifier.weight(0.77f).fillMaxHeight(),
                    verticalArrangement = Arrangement.spacedBy(gap),
                ) {
                    // Faixa superior menor: quatro leituras grandes, sem barras animadas.
                    Row(
                        Modifier.fillMaxWidth().height(if (compact) 78.dp else 92.dp),
                        horizontalArrangement = Arrangement.spacedBy(gap),
                    ) {
                        MetricCard("Velocidade", state.speedKmh.value, "km/h", Modifier.weight(1f), compact)
                        MetricCard("RPM", state.engine.rpm.value, "rpm", Modifier.weight(1f), compact)
                        GearCard(state, Modifier.weight(1.05f), compact)
                        EcoTopCard(state, Modifier.weight(0.92f), compact)
                    }

                    // Centro: Sensor em Destaque estreito e alto + seis sensores fixos ao lado.
                    Row(
                        Modifier.fillMaxWidth().weight(1f),
                        horizontalArrangement = Arrangement.spacedBy(gap),
                    ) {
                        RotatingSensorCard(
                            state = state,
                            modifier = Modifier.weight(0.40f).fillMaxHeight(),
                            compact = compact,
                        )
                        PrimarySensorsGrid(
                            state = state,
                            modifier = Modifier.weight(0.60f).fillMaxHeight(),
                            compact = compact,
                        )
                    }
                }

                TripPanel(
                    state = state,
                    modifier = Modifier.weight(0.23f).fillMaxHeight(),
                    compact = compact,
                    onResetSession = onResetSession,
                )
            }
        }
    }
}

@Composable
private fun MetricCard(
    label: String,
    value: Double?,
    unit: String,
    modifier: Modifier,
    compact: Boolean,
) {
    TruckCard(modifier) {
        Text(label, fontWeight = FontWeight.Bold, fontSize = if (compact) 10.sp else 12.sp)
        Spacer(Modifier.weight(1f))
        Row(verticalAlignment = Alignment.Bottom) {
            Text(
                value?.roundToInt()?.toString() ?: "—",
                fontWeight = FontWeight.Black,
                fontStyle = FontStyle.Italic,
                fontSize = if (compact) 27.sp else 34.sp,
            )
            Spacer(Modifier.width(4.dp))
            Text(
                unit,
                color = TruckMuted,
                fontSize = if (compact) 8.sp else 10.sp,
                modifier = Modifier.padding(bottom = 4.dp),
            )
        }
    }
}

@Composable
private fun GearCard(state: TruckState, modifier: Modifier, compact: Boolean) {
    val t = state.transmission
    val can = state.connection.canActive
    val current = if (!can || t.currentGear == null) "—" else if (t.currentGear == 0) "N" else t.currentGear.toString()
    val confirmed = t.prediction.confirmedGear ?: t.selectedGear?.takeIf { it > 0 && it != t.currentGear }
    val predicted = t.prediction.predictedGear?.takeIf { it > 0 && it != t.currentGear }

    TruckCard(modifier, accentBorder = confirmed != null || t.prediction.phase == "PREDICTED") {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Marcha", fontWeight = FontWeight.Bold, fontSize = if (compact) 10.sp else 12.sp)
            Spacer(Modifier.weight(1f))
            t.ishiftMode?.let {
                Text(it, color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold, fontSize = if (compact) 8.sp else 10.sp)
            }
        }
        Spacer(Modifier.weight(1f))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(current, fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic, fontSize = if (compact) 27.sp else 34.sp)
            when {
                confirmed != null -> Text(" → $confirmed", color = StatusOk, fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic, fontSize = if (compact) 18.sp else 23.sp)
                t.prediction.phase == "PREDICTED" && predicted != null -> Text(" ⇢ $predicted", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic, fontSize = if (compact) 18.sp else 23.sp)
            }
        }
        when {
            confirmed != null -> Text("I-SHIFT", color = StatusOk, fontSize = if (compact) 7.sp else 9.sp, fontWeight = FontWeight.Bold)
            t.prediction.phase == "PREDICTED" && predicted != null -> {
                val score = (t.prediction.score * 100).roundToInt()
                Text("PROVÁVEL $score%", color = MaterialTheme.colorScheme.primary, fontSize = if (compact) 7.sp else 9.sp, fontWeight = FontWeight.Bold)
            }
            else -> Text("I-Shift", color = TruckMuted, fontSize = if (compact) 7.sp else 9.sp)
        }
    }
}

@Composable
private fun EcoTopCard(state: TruckState, modifier: Modifier, compact: Boolean) {
    val score = EcoEfficiency.calculate(state.fuel, state.analytics).score
    TruckCard(modifier) {
        Text("Eco Score", fontWeight = FontWeight.Bold, fontSize = if (compact) 10.sp else 12.sp)
        Spacer(Modifier.weight(1f))
        Row(verticalAlignment = Alignment.Bottom) {
            Text(
                score?.toString() ?: "—",
                fontWeight = FontWeight.Black,
                fontStyle = FontStyle.Italic,
                fontSize = if (compact) 27.sp else 34.sp,
                color = if (score != null) StatusOk else LocalContentColor.current,
            )
            Text("/100", color = TruckMuted, fontWeight = FontWeight.Bold, fontSize = if (compact) 10.sp else 13.sp, modifier = Modifier.padding(bottom = 4.dp))
        }
    }
}

@Composable
private fun RotatingSensorCard(state: TruckState, modifier: Modifier, compact: Boolean) {
    // Lista aprovada para o loop do Sensor em Destaque.
    val slides = listOf(
        SensorSlide("Temperatura do óleo do motor", { state.engine.oilTempC.value }, "°C", 0),
        SensorSlide("Temperatura do óleo da caixa", { state.transmission.oilTempC.value }, "°C", 0),
        SensorSlide("Temperatura da água", { state.engine.coolantTempC.value }, "°C", 0),
        SensorSlide("Temperatura do ar de admissão", { state.engine.intakeAirTempC.value }, "°C", 0),
        SensorSlide("Pressão do óleo do motor", { state.engine.oilPressureBar.value }, "bar", 1),
        SensorSlide("Nível do óleo do motor", { state.engine.oilLevelPct.value }, "%", 0),
    )

    var idx by remember { mutableIntStateOf(0) }
    LaunchedEffect(idx, slides.size) {
        delay(4000)
        idx = (idx + 1) % slides.size
    }

    val sensor = slides[idx.coerceIn(0, slides.lastIndex)]
    val value = sensor.value()
    val valueText = when {
        value == null -> "—"
        sensor.decimals == 0 -> value.roundToInt().toString()
        sensor.decimals == 2 -> fmt2(value)
        else -> fmt1(value)
    }

    TruckCard(modifier = modifier, accentBorder = true) {
        Text(
            "SENSOR EM DESTAQUE",
            color = MaterialTheme.colorScheme.primary,
            fontWeight = FontWeight.Bold,
            fontSize = if (compact) 12.sp else 13.sp,
        )

        Column(
            Modifier.fillMaxWidth().weight(1f),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            Text(
                sensor.title,
                color = TruckMuted,
                fontWeight = FontWeight.Bold,
                fontSize = if (compact) 17.sp else 19.sp,
                maxLines = 2,
            )
            Spacer(Modifier.height(if (compact) 8.dp else 13.dp))
            Row(verticalAlignment = Alignment.Bottom) {
                Text(
                    valueText,
                    fontWeight = FontWeight.Black,
                    fontStyle = FontStyle.Italic,
                    fontSize = if (compact) 81.sp else 91.sp,
                )
                Spacer(Modifier.width(6.dp))
                Text(sensor.unit, color = TruckMuted, fontSize = if (compact) 18.sp else 20.sp, modifier = Modifier.padding(bottom = 8.dp))
            }
        }

        // Apenas indicação passiva do loop; sem setas e sem botão de pausa.
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            slides.indices.forEach { dot ->
                Box(
                    Modifier
                        .padding(horizontal = 3.dp)
                        .size(if (dot == idx) 7.dp else 5.dp)
                        .background(if (dot == idx) MaterialTheme.colorScheme.primary else TruckBorder, CircleShape)
                )
            }
        }
    }
}

@Composable
private fun PrimarySensorsGrid(state: TruckState, modifier: Modifier, compact: Boolean) {
    Column(modifier, verticalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 9.dp)) {
        Row(Modifier.weight(1f), horizontalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 9.dp)) {
            SmallSensor("Pressão turbo", fmt2(state.engine.boostBar.value), "bar", Modifier.weight(1f), compact)
            SmallSensor("Pressão combustível", fmt1(state.engine.fuelPressureBar.value), "bar", Modifier.weight(1f), compact)
        }
        Row(Modifier.weight(1f), horizontalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 9.dp)) {
            SmallSensor("Pedal acelerador", fmt0(state.engine.acceleratorPct.value), "%", Modifier.weight(1f), compact)
            SmallSensor("Desliz. embreagem", fmt1(state.transmission.clutchSlipPct.value), "%", Modifier.weight(1f), compact)
        }
        Row(Modifier.weight(1f), horizontalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 9.dp)) {
            SmallSensor("Torque motor", fmt0(state.engine.actualTorquePct.value), "%", Modifier.weight(1f), compact)
            SmallSensor("Peso do bogie", state.weights.driveBogieKg.value?.let { fmt1(it / 1000.0) } ?: "—", "t", Modifier.weight(1f), compact)
        }
    }
}

@Composable
private fun SmallSensor(label: String, value: String, unit: String, modifier: Modifier, compact: Boolean) {
    Surface(
        modifier,
        shape = RoundedCornerShape(14.dp),
        color = TruckSurface3,
        border = BorderStroke(1.dp, TruckBorder),
    ) {
        Column(
            Modifier.fillMaxSize().padding(if (compact) 7.dp else 10.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            Text(label, color = TruckMuted, fontSize = if (compact) 12.sp else 13.sp, maxLines = 2)
            Spacer(Modifier.height(if (compact) 3.dp else 6.dp))
            Row(verticalAlignment = Alignment.Bottom) {
                Text(value, fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic, fontSize = if (compact) 39.sp else 44.sp)
                Spacer(Modifier.width(3.dp))
                Text(unit, color = TruckMuted, fontSize = if (compact) 11.sp else 12.sp, modifier = Modifier.padding(bottom = 4.dp))
            }
        }
    }
}

@Composable
private fun TripPanel(
    state: TruckState,
    modifier: Modifier,
    compact: Boolean,
    onResetSession: () -> Unit,
) {
    val f = state.fuel
    val speed = state.speedKmh.value
    val rate = f.fuelRateLph.value
    val instant = if (speed != null && speed > 10.0 && rate != null && rate > 0.05) {
        "${fmt2(speed / rate)} km/L"
    } else {
        "${fmt1(rate)} L/h"
    }
    var confirmReset by remember { mutableStateOf(false) }

    TruckCard(modifier) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                if (state.operation.mode == OperationMode.NO_TRIP) "SESSÃO DE CAMPO" else "VIAGEM ATUAL",
                color = MaterialTheme.colorScheme.primary,
                fontWeight = FontWeight.Bold,
                fontSize = if (compact) 14.sp else 16.sp,
            )
            Spacer(Modifier.weight(1f))
            TextButton(
                onClick = { confirmReset = true },
                contentPadding = PaddingValues(horizontal = 5.dp, vertical = 0.dp),
            ) {
                Text("Zerar", fontSize = if (compact) 11.sp else 12.sp)
            }
        }
        Spacer(Modifier.height(if (compact) 3.dp else 7.dp))
        TripLine("Distância", if (f.distanceReady) "${fmt1(f.sessionDistanceKm)} km" else "—", compact)
        TripLine("Tempo", formatDuration(state.connection.sessionSeconds), compact)
        TripLine("Litros consumidos", if (f.fuelReady) "${fmt1(f.sessionLiters)} L" else "—", compact)
        TripLine("Média", f.sessionAverageKml?.let { "${fmt2(it)} km/L" } ?: "—", compact)
        TripLine("Últimos 50 km", f.last50KmAverageKml?.let { "${fmt2(it)} km/L" } ?: "coletando", compact)
        TripLine("Peso conjunto CAN", state.weights.combinationKg.value?.let { "${fmt1(it / 1000.0)} t" } ?: "—", compact)
        Spacer(Modifier.height(if (compact) 6.dp else 10.dp))
        Text("CONSUMO INSTANTÂNEO", color = TruckMuted, fontSize = if (compact) 11.sp else 12.sp)
        Text(
            instant,
            fontWeight = FontWeight.Black,
            fontStyle = FontStyle.Italic,
            fontSize = if (compact) 30.sp else 34.sp,
            color = MaterialTheme.colorScheme.primary,
        )
        Spacer(Modifier.weight(1f))
    }

    if (confirmReset) {
        AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text("Zerar sessão atual?") },
            text = { Text("Distância, litros, média, tempo e indicadores da sessão voltarão a zero. Configurações e logs não serão apagados.") },
            confirmButton = {
                TextButton(onClick = {
                    confirmReset = false
                    onResetSession()
                }) { Text("Zerar") }
            },
            dismissButton = { TextButton(onClick = { confirmReset = false }) { Text("Cancelar") } },
        )
    }
}

@Composable
private fun TripLine(label: String, value: String, compact: Boolean) {
    Column(Modifier.fillMaxWidth().padding(vertical = if (compact) 2.dp else 4.dp)) {
        Text(label, color = TruckMuted, fontSize = if (compact) 11.sp else 12.sp)
        Text(
            value,
            fontWeight = FontWeight.Bold,
            fontStyle = FontStyle.Italic,
            fontSize = if (compact) 20.sp else 22.sp,
            maxLines = 1,
        )
    }
    HorizontalDivider(color = TruckBorder.copy(alpha = 0.5f))
}
