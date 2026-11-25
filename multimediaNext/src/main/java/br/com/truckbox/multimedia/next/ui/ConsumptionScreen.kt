package br.com.truckbox.multimedia.next.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke as LineStroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import br.com.truckbox.multimedia.next.domain.*
import java.time.Instant
import kotlin.math.ceil
import kotlin.math.max

@Composable fun ConsumptionScreen(state: AppState, now: Instant) {
    var legIndex by remember(state.legs.map { it.kind }) {
        mutableIntStateOf(LegKind.entries.indexOfFirst { kind -> state.legs.any { it.kind == kind } }.coerceAtLeast(0))
    }
    ChoiceRow(LegKind.entries.map { it.title }, legIndex, { legIndex = it })
    val leg = state.legs.singleOrNull { it.kind == LegKind.entries[legIndex] }
    val local = state.localFuel?.takeIf { it.coreUid == state.coreDeviceUid && it.fresh(now) }
    val localTrip = local?.trip?.takeIf { state.trip == null && state.legs.isEmpty() }
    val flow = displayedConsumption(state.speed, state.fuelRate, local, now)
    val balance = state.fuelRange
    val history = state.fuelHistory?.takeIf { it.rows.isNotEmpty() } ?: state.localFuelHistory?.takeIf {
        it.coreUid == state.coreDeviceUid && (state.expectedCoreUid == null || it.coreUid == state.expectedCoreUid)
    }
    val last100 = remember(history) {
        history?.takeIf { it.throughKm >= 100 }?.let { summarize(it.between(it.throughKm - 100, it.throughKm)) }
    }
    val forecast = state.routeForecast
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Metric("Média da viagem / etapa", format(leg?.averageKml ?: localTrip?.averageKml, 2), "km/L", Modifier.weight(1f),
            leg?.fuel?.label(now) ?: localTrip?.label)
        Metric("Últimos 100 km", format(last100?.average ?: state.last100Kml.takeIf { state.demo }, 2), "km/L", Modifier.weight(1f),
            last100?.let { "${format(it.distance, 1)} km observados · ${history?.source.orEmpty()}" })
        Metric("Consumo instantâneo", format(flow?.value, 2) ?: "Corte".takeIf { flow?.explanation?.startsWith("Corte") == true },
            flow?.unit ?: "km/L · L/h", Modifier.weight(1f),
            flow?.explanation ?: "Leitura indisponível")
    }
    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
        Metric("Litros consumidos da etapa", format(leg?.consumedLiters ?: localTrip?.litres), "L", Modifier.weight(1f))
        Metric("Distância da etapa", format(leg?.distanceKm ?: localTrip?.distanceKm), "km", Modifier.weight(1f))
        val tankNote = balance?.let {
            listOfNotNull(it.percent?.let { value -> "${format(value, 1)}%" },
                it.capacityLiters?.let { value -> "capacidade ${format(value, 0)} L" }, it.qualityLabel,
                "Cloud · ${dataAge(it.updatedAt, now)}").joinToString(" · ")
        } ?: state.tankStatus.takeIf { state.demo } ?: "Aguardando saldo da Cloud"
        Metric("Diesel estimado no tanque", format(balance?.remainingLiters ?: state.tankLiters.takeIf { state.demo }), "L",
            Modifier.weight(1f), tankNote)
        val range = forecast?.range
        val conventional = balance?.autonomyKm
        val estimated = range?.reachM?.div(1_000) ?: conventional
        val rangeNote = if (range != null) {
            "Até este ponto da rota · ${confidence(forecast.confidence)} · ${dataAge(forecast.computedAt, now)}"
        } else balance?.let {
            "${it.averageLabel} · ${format(it.averageKml, 2) ?: "—"} km/L · ${it.qualityLabel} · ${dataAge(it.updatedAt, now)}"
        }
        Metric(if (range != null) "Autonomia inteligente" else "Autonomia convencional", format(estimated, 0),
            "km", Modifier.weight(1f), rangeNote)
    }
    if (forecast?.range != null && balance?.autonomyKm != null) {
        Caption("Autonomia convencional ${format(balance.autonomyKm, 0)} km · ${balance.averageLabel}")
    }
    FuelHistoryPanel(history, now)
    RouteFuelPanel(state.route, forecast, now)
    RotatingEfficiency(state, Modifier.fillMaxWidth())
}

private data class FuelTotals(val distance: Double, val used: Double, val idle: Double?, val planned: Double?) {
    val average get() = if (used > 0) distance / used else null
}
private fun summarize(rows: List<FuelInterval>): FuelTotals? = if (rows.isEmpty()) null else FuelTotals(
    rows.sumOf { it.coveredKm }, rows.sumOf { it.liters },
    if (rows.all { it.idleLiters != null }) rows.sumOf { requireNotNull(it.idleLiters) } else null,
    if (rows.all { it.plannedLiters != null }) rows.sumOf { requireNotNull(it.plannedLiters) } else null,
)

@Composable private fun FuelHistoryPanel(history: FuelHistory?, now: Instant) {
    var period by remember { mutableIntStateOf(1) }
    var block by remember { mutableIntStateOf(0) }
    PanelCard(Modifier.fillMaxWidth()) {
        SectionTitle("Evolução do consumo")
        ChoiceRow(listOf("Últimos 10 km", "Últimos 50 km", "Últimos 100 km", "Viagem", "Blocos de 50 km"), period, { period = it })
        if (history == null || history.rows.isEmpty()) {
            Caption("Ainda sem intervalos medidos para o gráfico.")
        } else {
            val limit = max(0, ceil(history.throughKm / 50.0).toInt() - 1)
            val selectedBlock = block.coerceIn(0, limit)
            val from = when (period) {
                0 -> max(0.0, history.throughKm - 10)
                1 -> max(0.0, history.throughKm - 50)
                2 -> max(0.0, history.throughKm - 100)
                4 -> selectedBlock * 50.0
                else -> 0.0
            }
            val to = if (period == 4) minOf(history.throughKm, from + 50) else history.throughKm
            if (period == 4) Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedButton({ block = selectedBlock - 1 }, enabled = selectedBlock > 0) { Text("Anterior") }
                Text("${selectedBlock * 50}–${(selectedBlock + 1) * 50} km", modifier = Modifier.padding(top = 12.dp))
                OutlinedButton({ block = selectedBlock + 1 }, enabled = selectedBlock < limit) { Text("Próximo") }
            }
            val rows = remember(history, from, to) { history.between(from, to) }
            val totals = remember(rows) { summarize(rows) }
            Caption("${history.source} · ${dataAge(history.sampledAt, now)}")
            if (totals == null) Caption("Nenhum intervalo medido neste trecho.")
            else {
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Metric("Média observada", format(totals.average, 2), "km/L", Modifier.weight(1f))
                    Metric("Diesel observado", format(totals.used, 2), "L", Modifier.weight(1f))
                    Metric("Distância com cobertura", format(totals.distance, 2), "km", Modifier.weight(1f),
                        "Janela de ${format(to - from, 1)} km")
                    Metric("Diesel em marcha lenta", format(totals.idle, 2), "L", Modifier.weight(1f))
                }
                FuelHistoryChart(rows, from, to)
                val elevations = remember(rows, from) {
                    rows.mapNotNull { interval -> interval.altitudeM?.let { ElevationPoint((interval.toKm - from) * 1_000, it) } }
                        .distinctBy { it.distanceM }
                }
                if (elevations.size >= 2) ElevationChart(elevations, "Altitudes registradas no trecho")
                val half = (from + to) / 2
                val start = remember(rows, half) { summarize(rows.filter { it.toKm <= half })?.average }
                val finish = remember(rows, half) { summarize(rows.filter { it.fromKm >= half })?.average }
                if (start != null && finish != null) Caption("Média da primeira metade ${format(start, 2)} km/L · segunda metade ${format(finish, 2)} km/L")
                totals.planned?.let { Caption("Previsto no mesmo trecho ${format(it, 2)} L · realizado ${format(totals.used, 2)} L") }
                Caption("Trechos sem cobertura ficam fora da média.")
            }
        }
    }
}

@Composable private fun FuelHistoryChart(rows: List<FuelInterval>, from: Double, to: Double) {
    val chart = remember(rows) {
        var km = 0.0
        var diesel = 0.0
        var previousEnd: Double? = null
        rows.mapNotNull {
            km += it.coveredKm
            diesel += it.liters
            val breaks = it.gapBefore || (previousEnd?.let { prior -> it.fromKm > prior + .05 } ?: true)
            previousEnd = it.toKm
            if (diesel > 0) ChartSample(it.toKm, km / diesel, breaks) else null
        }
    }
    if (chart.size < 2 || to <= from) return
    Caption("Evolução da média acumulada · km/L")
    val peak = chart.maxOf { it.value }.coerceAtLeast(.01)
    Caption("${format(peak, 2)} km/L")
    Canvas(Modifier.fillMaxWidth().height(108.dp)) {
        drawLine(Muted, Offset(0f, size.height), Offset(size.width, size.height), 1.dp.toPx())
        val path = Path()
        chart.forEachIndexed { index, item ->
            val x = ((item.km - from) / (to - from) * size.width).toFloat()
            val y = ((1 - item.value / peak) * size.height).toFloat()
            if (index == 0 || item.breaks) path.moveTo(x, y) else path.lineTo(x, y)
        }
        drawPath(path, Orange, style = LineStroke(2.dp.toPx()))
    }
    Caption("${format(from, 1)} km                                    ${format(to, 1)} km")
}
private data class ChartSample(val km: Double, val value: Double, val breaks: Boolean)

@Composable internal fun RouteFuelPanel(route: JourneyRoute?, forecast: RouteFuelForecast?, now: Instant) {
    PanelCard(Modifier.fillMaxWidth()) {
        SectionTitle("Previsão da rota")
        if (forecast == null) {
            Caption(if (route == null) "Aguardando rota e previsão topográfica da Cloud."
            else "A rota está disponível. A previsão por relevo e histórico ainda não chegou da Cloud.")
        } else {
            Caption("${confidence(forecast.confidence)} · modelo ${forecast.modelVersion} · ${forecast.historySamples} intervalos de histórico · ${dataAge(forecast.computedAt, now)}")
        }
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            HorizonCard("Próximos 50 km", forecast?.next50, Modifier.weight(1f))
            HorizonCard("Próximos 100 km", forecast?.next100, Modifier.weight(1f))
            HorizonCard("Até o destino", forecast?.destination, Modifier.weight(1f))
        }
        val profile = route?.elevation.orEmpty()
        if (profile.size >= 2) ElevationChart(profile)
        forecast?.range?.let {
            Caption("Reserva de ${format(it.reserveLiters, 1)} L · déficit previsto de ${format(it.deficitLiters, 1)} L")
            if (it.deficitLiters > 0) Text("Abastecimento necessário para chegar com a reserva", color = Orange)
        }
        forecast?.comparison?.let {
            Caption(it.originalAt?.let { at -> "Previsão original registrada em $at" }
                ?: "Comparação desde o primeiro cálculo, em ${it.comparisonStartedAt}")
            Text("Previsto até a posição ${format(it.plannedToPositionLiters, 1) ?: "—"} L · realizado ${format(it.observedLiters, 1) ?: "—"} L · diferença ${format(it.differenceLiters, 1) ?: "—"} L")
            Caption("Projeção atualizada ${format(it.projectedTotalLiters, 1) ?: "—"} L · variação projetada ${format(it.projectedDifferenceLiters, 1) ?: "—"} L")
            it.differenceCents?.let { cents -> Caption("Diferença realizada ${fuelMoney(cents)}") }
            it.projectedDifferenceCents?.let { cents -> Caption("Diferença projetada ${fuelMoney(cents)}") }
        }
    }
}

@Composable private fun HorizonCard(title: String, data: FuelHorizon?, modifier: Modifier) {
    PanelCard(modifier) {
        Text(title, fontWeight = FontWeight.Bold)
        if (data == null) Caption("Indisponível")
        else {
            Text("${format(data.liters, 1)} L · ${format(data.distanceM / 1_000, 1)} km", fontWeight = FontWeight.Bold)
            data.averageKml?.let { Caption("${format(it, 2)} km/L") }
            val grade = when (data.terrain) {
                "ASCENT" -> "Predominância de subidas"
                "DESCENT" -> "Predominância de descidas"
                "FLAT" -> "Predominância de trechos planos"
                "MIXED" -> "Terreno misto"
                else -> null
            }
            grade?.let { Caption(it) }
            if (data.ascentM != null || data.descentM != null) Caption("↑ ${format(data.ascentM, 0) ?: "—"} m · ↓ ${format(data.descentM, 0) ?: "—"} m")
            data.fuelAtEndLiters?.let { Caption("Saldo previsto ${format(it, 1)} L") }
        }
    }
}

@Composable internal fun ElevationChart(points: List<ElevationPoint>, label: String = "Perfil de elevação planejado") {
    val low = points.minOf { it.altitudeM }
    val high = points.maxOf { it.altitudeM }
    val end = points.last().distanceM.coerceAtLeast(1.0)
    Caption("$label · ${format(low, 0)}–${format(high, 0)} m")
    Canvas(Modifier.fillMaxWidth().height(86.dp)) {
        val line = Path()
        points.forEachIndexed { index, p ->
            val x = (p.distanceM / end * size.width).toFloat()
            val y = (size.height - ((p.altitudeM - low) / (high - low).coerceAtLeast(1.0) * size.height)).toFloat()
            if (index == 0) line.moveTo(x, y) else line.lineTo(x, y)
        }
        drawPath(line, Orange, style = LineStroke(2.dp.toPx()))
    }
}

private fun fuelMoney(cents: Long) = java.text.NumberFormat.getCurrencyInstance(java.util.Locale.forLanguageTag("pt-BR")).format(cents / 100.0)

private fun confidence(level: String) = when (level) {
    "HIGH" -> "Confiança alta"
    "MEDIUM" -> "Confiança média"
    else -> "Confiança baixa"
}
