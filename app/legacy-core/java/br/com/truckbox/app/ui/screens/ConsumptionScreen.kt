package br.com.truckbox.app.ui.screens

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.app.data.TruckState
import br.com.truckbox.app.domain.efficiency.EcoBreakdown
import br.com.truckbox.app.domain.efficiency.EcoCriterion
import br.com.truckbox.app.domain.efficiency.EcoEfficiency
import br.com.truckbox.app.ui.theme.*

/**
 * Tela Consumo v0.4.1, otimizada para multimídia 13" em paisagem.
 *
 * Hierarquia aprovada:
 * 1) média geral da viagem
 * 2) últimos 100 km + GPS
 * 3) consumo instantâneo
 * 4) litros consumidos na viagem
 * 5) combustível estimado no tanque
 * 6) autonomia pelos últimos 100 km
 * 7) histórico de médias
 * 8) Eco Score + economia real + critérios de eficiência
 */
@Composable
fun ConsumptionScreen(
    state: TruckState,
    tankCapacityLiters: Double = 0.0,
    tankRemainingLiters: Double? = null,
    tankPercent: Double? = null,
    tankRangeKm: Double? = null,
    operationFuelLiters: Double? = null,
    operationDistanceKm: Double? = null,
    operationAverageKml: Double? = null,
) {
    val fuel = state.fuel
    val tripAverage = operationAverageKml ?: fuel.sessionAverageKml
    val tripFuel = operationFuelLiters ?: if (fuel.fuelReady) fuel.sessionLiters else null
    val speed = state.speedKmh.value ?: 0.0
    val lph = fuel.fuelRateLph.value
    val instant = if (speed > 10.0 && lph != null && lph > 0.05) speed / lph else lph
    val instantUnit = if (speed > 10.0) "km/L" else "L/h"
    val eco = EcoEfficiency.calculate(fuel, state.analytics)

    Column(Modifier.fillMaxSize()) {
        TruckHeader(state)
        BoxWithConstraints(Modifier.fillMaxWidth().weight(1f)) {
            val compact = maxHeight < 590.dp
            val gap = if (compact) 7.dp else 10.dp
            val outer = if (compact) 8.dp else 11.dp
            val topHeight = if (compact) 108.dp else 125.dp
            val secondHeight = if (compact) 82.dp else 96.dp

            Column(
                Modifier.fillMaxSize().padding(horizontal = outer, vertical = if (compact) 6.dp else 9.dp),
                verticalArrangement = Arrangement.spacedBy(gap),
            ) {
                Row(
                    Modifier.fillMaxWidth().height(topHeight),
                    horizontalArrangement = Arrangement.spacedBy(gap),
                ) {
                    BigConsumptionCard(
                        title = "MÉDIA GERAL DA VIAGEM",
                        value = tripAverage,
                        unit = "km/L",
                        icon = Icons.Filled.Speed,
                        modifier = Modifier.weight(0.43f),
                        accent = true,
                        compact = compact,
                    )
                    Last100Card(
                        state = state,
                        tripAverageKml = tripAverage,
                        modifier = Modifier.weight(0.57f),
                        compact = compact,
                    )
                }

                Row(
                    Modifier.fillMaxWidth().height(secondHeight),
                    horizontalArrangement = Arrangement.spacedBy(gap),
                ) {
                    SmallConsumptionCard(
                        title = "CONSUMO INSTANTÂNEO",
                        value = instant,
                        unit = instantUnit,
                        icon = Icons.Filled.LocalGasStation,
                        modifier = Modifier.weight(1f),
                        compact = compact,
                        decimals = 2,
                    )
                    SmallConsumptionCard(
                        title = "CONSUMIDO NA VIAGEM",
                        value = tripFuel,
                        unit = "L",
                        icon = Icons.Filled.OilBarrel,
                        modifier = Modifier.weight(1f),
                        compact = compact,
                        decimals = 1,
                    )
                    SmallConsumptionCard(
                        title = "NO TANQUE",
                        value = tankRemainingLiters,
                        unit = "L",
                        icon = Icons.Filled.LocalGasStation,
                        modifier = Modifier.weight(1f),
                        compact = compact,
                        decimals = 0,
                        footer = when {
                            tankCapacityLiters <= 0.0 -> "Configure a capacidade"
                            tankRemainingLiters == null -> "Calibre em tanque cheio"
                            tankPercent != null -> "${fmt0(tankPercent)}% de ${fmt0(tankCapacityLiters)} L"
                            else -> "capacidade ${fmt0(tankCapacityLiters)} L"
                        },
                    )
                    SmallConsumptionCard(
                        title = "AUTONOMIA",
                        value = tankRangeKm,
                        unit = "km",
                        icon = Icons.Filled.Route,
                        modifier = Modifier.weight(1f),
                        compact = compact,
                        decimals = 0,
                        footer = if (fuel.last100KmAverageKml != null)
                            "base ${fmt2(fuel.last100KmAverageKml)} km/L • últimos 100 km"
                        else "aguardando média dos últimos 100 km",
                    )
                }

                Row(
                    Modifier.fillMaxWidth().weight(1f),
                    horizontalArrangement = Arrangement.spacedBy(gap),
                ) {
                    Column(
                        Modifier.weight(0.68f).fillMaxHeight(),
                        verticalArrangement = Arrangement.spacedBy(gap),
                    ) {
                        TripHistoryPanel(
                            state = state,
                            modifier = Modifier.weight(0.55f).fillMaxWidth(),
                            compact = compact,
                        )
                        EfficiencyCriteriaPanel(
                            eco = eco,
                            modifier = Modifier.weight(0.45f).fillMaxWidth(),
                            compact = compact,
                        )
                    }

                    Column(
                        Modifier.weight(0.32f).fillMaxHeight(),
                        verticalArrangement = Arrangement.spacedBy(gap),
                    ) {
                        EcoScorePanel(eco, Modifier.weight(0.34f).fillMaxWidth(), compact)
                        RealSavingsPanel(Modifier.weight(0.27f).fillMaxWidth(), compact)
                        AssistantPanel(state, eco, Modifier.weight(0.39f).fillMaxWidth(), compact)
                    }
                }
            }
        }
    }
}

@Composable
private fun SmallConsumptionCard(
    title: String,
    value: Double?,
    unit: String,
    icon: ImageVector,
    modifier: Modifier,
    compact: Boolean,
    decimals: Int = 1,
    footer: String? = null,
) {
    TruckCard(modifier) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Icon(
                icon, null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(if (compact) 21.dp else 25.dp),
            )
            Spacer(Modifier.width(if (compact) 6.dp else 8.dp))
            Text(title, color = TruckMuted, fontWeight = FontWeight.Bold, fontSize = if (compact) 9.sp else 10.sp, maxLines = 1)
        }
        Spacer(Modifier.weight(1f))
        Row(verticalAlignment = Alignment.Bottom) {
            val number = when {
                value == null -> "—"
                decimals <= 0 -> fmt0(value)
                decimals == 1 -> fmt1(value)
                else -> fmt2(value)
            }
            Text(
                number,
                fontWeight = FontWeight.Black,
                fontStyle = FontStyle.Italic,
                fontSize = if (compact) 29.sp else 35.sp,
                maxLines = 1,
            )
            Spacer(Modifier.width(5.dp))
            Text(
                unit,
                color = MaterialTheme.colorScheme.primary,
                fontWeight = FontWeight.Bold,
                fontSize = if (compact) 12.sp else 14.sp,
                modifier = Modifier.padding(bottom = 4.dp),
            )
        }
        if (!footer.isNullOrBlank()) {
            Text(footer, color = TruckMuted, fontSize = if (compact) 8.sp else 9.sp, maxLines = 1)
        }
    }
}

@Composable
private fun BigConsumptionCard(
    title: String,
    value: Double?,
    unit: String,
    icon: ImageVector,
    modifier: Modifier,
    accent: Boolean,
    compact: Boolean,
    decimals: Int = 2,
) {
    TruckCard(modifier, accentBorder = accent) {
        Row(Modifier.fillMaxSize(), verticalAlignment = Alignment.CenterVertically) {
            Surface(
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.08f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.28f)),
                shape = RoundedCornerShape(14.dp),
                modifier = Modifier.size(if (compact) 58.dp else 70.dp),
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(icon, null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(if (compact) 31.dp else 38.dp))
                }
            }
            Spacer(Modifier.width(if (compact) 12.dp else 16.dp))
            Column(Modifier.weight(1f)) {
                Text(title, color = TruckMuted, fontWeight = FontWeight.Bold, fontSize = if (compact) 11.sp else 13.sp)
                Spacer(Modifier.height(if (compact) 1.dp else 3.dp))
                Row(verticalAlignment = Alignment.Bottom) {
                    val text = when {
                        value == null -> "—"
                        decimals == 1 -> fmt1(value)
                        else -> fmt2(value)
                    }
                    Text(
                        text,
                        fontWeight = FontWeight.Black,
                        fontStyle = FontStyle.Italic,
                        fontSize = if (compact) 45.sp else 54.sp,
                        maxLines = 1,
                    )
                    Spacer(Modifier.width(7.dp))
                    Text(
                        unit,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.Bold,
                        fontStyle = FontStyle.Italic,
                        fontSize = if (compact) 16.sp else 19.sp,
                        modifier = Modifier.padding(bottom = if (compact) 5.dp else 7.dp),
                    )
                }
            }
        }
    }
}

@Composable
private fun Last100Card(state: TruckState, tripAverageKml: Double?, modifier: Modifier, compact: Boolean) {
    val trip = tripAverageKml
    val last100 = state.fuel.last100KmAverageKml
    val diffPct = if (trip != null && trip > 0.0 && last100 != null) (last100 / trip - 1.0) * 100.0 else null
    val diffColor = when {
        diffPct == null -> TruckMuted
        diffPct >= 0.0 -> StatusOk
        else -> StatusWarn
    }
    TruckCard(modifier) {
        Row(Modifier.fillMaxSize(), verticalAlignment = Alignment.CenterVertically) {
            Surface(
                color = MaterialTheme.colorScheme.primary.copy(alpha = 0.08f),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.primary.copy(alpha = 0.28f)),
                shape = RoundedCornerShape(14.dp),
                modifier = Modifier.size(if (compact) 58.dp else 70.dp),
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Icon(Icons.Filled.Route, null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(if (compact) 31.dp else 38.dp))
                }
            }
            Spacer(Modifier.width(if (compact) 12.dp else 16.dp))
            Column(Modifier.weight(1f)) {
                Text("ÚLTIMOS 100 KM", color = TruckMuted, fontWeight = FontWeight.Bold, fontSize = if (compact) 11.sp else 13.sp)
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(
                        last100?.let { fmt2(it) } ?: "coletando",
                        fontWeight = FontWeight.Black,
                        fontStyle = FontStyle.Italic,
                        fontSize = if (last100 == null) (if (compact) 24.sp else 28.sp) else (if (compact) 45.sp else 54.sp),
                        maxLines = 1,
                    )
                    if (last100 != null) {
                        Spacer(Modifier.width(7.dp))
                        Text("km/L", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold, fontStyle = FontStyle.Italic, fontSize = if (compact) 16.sp else 19.sp, modifier = Modifier.padding(bottom = 6.dp))
                    }
                }
                Text(
                    if (state.gps.hasFix) "GPS ativo • janela móvel de 100 km" else "Aguardando GPS externo/Core para identificar o trecho",
                    color = if (state.gps.hasFix) MaterialTheme.colorScheme.primary else TruckMuted,
                    fontSize = if (compact) 9.sp else 10.sp,
                    maxLines = 1,
                )
            }
            Column(horizontalAlignment = Alignment.End) {
                Text(
                    diffPct?.let { (if (it >= 0) "+" else "") + fmt1(it) + "%" } ?: "—",
                    color = diffColor,
                    fontWeight = FontWeight.Black,
                    fontStyle = FontStyle.Italic,
                    fontSize = if (compact) 24.sp else 30.sp,
                )
                Text("vs média da viagem", color = TruckMuted, fontSize = if (compact) 9.sp else 10.sp)
            }
        }
    }
}

@Composable
private fun TripHistoryPanel(state: TruckState, modifier: Modifier, compact: Boolean) {
    TruckCard(modifier) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("HISTÓRICO DAS MÉDIAS DAS VIAGENS", fontWeight = FontWeight.Bold, fontSize = if (compact) 12.sp else 14.sp)
            Spacer(Modifier.weight(1f))
            Text("Últimas viagens", color = TruckMuted, fontSize = if (compact) 9.sp else 10.sp)
        }
        Spacer(Modifier.height(if (compact) 6.dp else 8.dp))

        // O cadastro/encerramento de viagens ainda será conectado ao histórico persistente.
        // Nesta versão não exibimos barras fictícias. A viagem atual aparece como referência real.
        Row(Modifier.fillMaxSize(), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(0.30f)) {
                Text("VIAGEM ATUAL", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold, fontSize = if (compact) 10.sp else 11.sp)
                Text(
                    state.fuel.sessionAverageKml?.let { "${fmt2(it)} km/L" } ?: "—",
                    fontWeight = FontWeight.Black,
                    fontStyle = FontStyle.Italic,
                    fontSize = if (compact) 28.sp else 34.sp,
                )
                Text("${fmt1(state.fuel.sessionDistanceKm)} km percorridos", color = TruckMuted, fontSize = if (compact) 9.sp else 10.sp)
            }
            Spacer(Modifier.width(10.dp))
            Box(
                Modifier.weight(0.70f).fillMaxHeight().background(TruckSurface3.copy(alpha = 0.50f), RoundedCornerShape(12.dp)),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    "O gráfico será preenchido automaticamente conforme as viagens forem encerradas e salvas.",
                    color = TruckMuted,
                    fontSize = if (compact) 10.sp else 11.sp,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.padding(horizontal = 18.dp),
                )
            }
        }
    }
}

@Composable
private fun EfficiencyCriteriaPanel(eco: EcoBreakdown, modifier: Modifier, compact: Boolean) {
    TruckCard(modifier) {
        Text("CRITÉRIOS DE EFICIÊNCIA", fontWeight = FontWeight.Bold, fontSize = if (compact) 12.sp else 14.sp)
        Spacer(Modifier.height(if (compact) 5.dp else 7.dp))
        Row(Modifier.fillMaxSize(), horizontalArrangement = Arrangement.spacedBy(if (compact) 5.dp else 7.dp)) {
            eco.criteria.forEach { criterion ->
                CriterionCard(criterion, Modifier.weight(1f).fillMaxHeight(), compact)
            }
        }
    }
}

@Composable
private fun CriterionCard(c: EcoCriterion, modifier: Modifier, compact: Boolean) {
    val score = c.score
    val color = scoreColor(score)
    Surface(
        modifier = modifier,
        color = TruckSurface3,
        border = BorderStroke(1.dp, TruckBorder),
        shape = RoundedCornerShape(11.dp),
    ) {
        Column(
            Modifier.fillMaxSize().padding(if (compact) 6.dp else 8.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(
                c.label,
                color = TruckMuted,
                fontWeight = FontWeight.Bold,
                fontSize = if (compact) 8.sp else 9.sp,
                textAlign = TextAlign.Center,
                maxLines = 2,
            )
            Text(
                score?.let { "$it%" } ?: "—",
                color = color,
                fontWeight = FontWeight.Black,
                fontStyle = FontStyle.Italic,
                fontSize = if (compact) 17.sp else 20.sp,
            )
            LinearProgressIndicator(
                progress = { ((score ?: 0) / 100f).coerceIn(0f, 1f) },
                modifier = Modifier.fillMaxWidth().height(if (compact) 4.dp else 5.dp),
                color = color,
                trackColor = TruckBorder,
            )
        }
    }
}

@Composable
private fun EcoScorePanel(eco: EcoBreakdown, modifier: Modifier, compact: Boolean) {
    val score = eco.score
    TruckCard(modifier) {
        Row(Modifier.fillMaxSize(), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Filled.Eco, null, tint = scoreColor(score), modifier = Modifier.size(if (compact) 39.dp else 48.dp))
            Spacer(Modifier.width(if (compact) 10.dp else 13.dp))
            Column(Modifier.weight(1f)) {
                Text("ECO SCORE", color = TruckMuted, fontWeight = FontWeight.Bold, fontSize = if (compact) 10.sp else 12.sp)
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(score?.toString() ?: "—", fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic, fontSize = if (compact) 39.sp else 48.sp, color = scoreColor(score))
                    Text("/100", color = TruckMuted, fontWeight = FontWeight.Bold, fontSize = if (compact) 12.sp else 14.sp, modifier = Modifier.padding(bottom = 6.dp))
                }
                Text(ecoLabel(score), color = scoreColor(score), fontSize = if (compact) 9.sp else 10.sp)
            }
        }
    }
}

@Composable
private fun RealSavingsPanel(modifier: Modifier, compact: Boolean) {
    TruckCard(modifier) {
        Row(Modifier.fillMaxSize(), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Filled.Savings, null, tint = StatusWarn, modifier = Modifier.size(if (compact) 34.dp else 42.dp))
            Spacer(Modifier.width(10.dp))
            Column {
                Text("ECONOMIA REAL", color = TruckMuted, fontWeight = FontWeight.Bold, fontSize = if (compact) 10.sp else 12.sp)
                Text("— L", color = StatusWarn, fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic, fontSize = if (compact) 27.sp else 33.sp)
                Text("linha de base equivalente em aprendizado", color = TruckMuted, fontSize = if (compact) 8.sp else 9.sp, maxLines = 1)
            }
        }
    }
}

@Composable
private fun AssistantPanel(state: TruckState, eco: EcoBreakdown, modifier: Modifier, compact: Boolean) {
    val trip = state.fuel.sessionAverageKml
    val last100 = state.fuel.last100KmAverageKml
    val weakest = eco.criteria.filter { it.score != null }.minByOrNull { it.score ?: 101 }
    val text = when {
        last100 != null && trip != null && trip > 0.0 -> {
            val diff = (last100 / trip - 1.0) * 100.0
            if (diff >= 0.0) "Últimos 100 km: ${fmt1(diff)}% acima da média da viagem."
            else "Últimos 100 km: ${fmt1(-diff)}% abaixo da média da viagem."
        }
        weakest != null && (weakest.score ?: 100) < 75 -> "Maior oportunidade agora: ${weakest.label.lowercase()}."
        state.analytics.movingSeconds < 60L -> "Coletando condução para formar o Eco Score."
        else -> "Condução estável. Continue coletando histórico por rota, peso e relevo."
    }

    TruckCard(modifier) {
        Row(Modifier.fillMaxSize(), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Filled.Psychology, null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(if (compact) 34.dp else 42.dp))
            Spacer(Modifier.width(10.dp))
            Column {
                Text("ASSISTENTE TRUCKBOX", color = TruckMuted, fontWeight = FontWeight.Bold, fontSize = if (compact) 10.sp else 12.sp)
                Spacer(Modifier.height(4.dp))
                Text(text, fontWeight = FontWeight.Bold, fontSize = if (compact) 10.sp else 12.sp, maxLines = 3)
                Text("Comparação equivalente entra após histórico suficiente.", color = TruckMuted, fontSize = if (compact) 8.sp else 9.sp, maxLines = 1)
            }
        }
    }
}

private fun scoreColor(score: Int?): Color = when {
    score == null -> TruckMuted
    score >= 85 -> StatusOk
    score >= 70 -> StatusWarn
    else -> StatusCritical
}

private fun ecoLabel(score: Int?): String = when {
    score == null -> "Coletando"
    score >= 90 -> "Condução muito eficiente"
    score >= 80 -> "Condução eficiente"
    score >= 70 -> "Boa, com oportunidades"
    else -> "Há oportunidades de economia"
}

