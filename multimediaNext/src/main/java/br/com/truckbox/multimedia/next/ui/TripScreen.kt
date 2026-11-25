package br.com.truckbox.multimedia.next.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke as RouteStroke
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.multimedia.next.domain.*
import java.time.Instant
import java.util.Locale

@Composable fun TripScreen(state: AppState,now: Instant) {
    var showJourney by remember { mutableStateOf(false) }
    var showFinance by remember { mutableStateOf(false) }
    var operational by remember { mutableStateOf(false) }
    LaunchedEffect(state.journeyAllowed,state.financialAllowed) {
        if(!state.journeyAllowed) showJourney=false
        if(!state.financialAllowed) showFinance=false
    }
    val trip=state.trip
    val route=state.route
    val localTrip=state.localFuel?.takeIf {trip==null && it.coreUid==state.coreDeviceUid && it.fresh(now)}?.trip
    PanelCard(Modifier.fillMaxWidth()) {
        Row(verticalAlignment=Alignment.CenterVertically,horizontalArrangement=Arrangement.spacedBy(18.dp)) {
            Column(Modifier.weight(1f),verticalArrangement=Arrangement.spacedBy(6.dp)) {
                Caption("OPERAÇÃO ATUAL · ${trip?.kind?.title ?: "NÃO VERIFICADA"}")
                Text(trip?.let{"${it.origin} → ${it.destination}"} ?: "Viagem atual não disponível",fontWeight=FontWeight.Bold,fontSize=22.sp)
                Caption(trip?.let{"${it.product ?: "Produto não informado"} · ${format(it.weightT) ?: "—"} t · cadastro no Motorista"} ?: "Aguardando vínculo e dados autorizados do Motorista/Cloud")
            }
            if(state.journeyAllowed) OutlinedButton(onClick={showJourney=true},modifier=Modifier.testTag("journey-button")) { Icon(Icons.Outlined.Schedule,null);Text("Jornada 24 h") }
        }
        val remaining=route?.remainingM?.div(1000)
        val progress=route?.remainingM?.let { ((route.distanceM - it) / route.distanceM).toFloat() }
        if(progress!=null) LinearProgressIndicator(progress={progress},modifier=Modifier.fillMaxWidth(),color=Orange,trackColor=Stroke)
        Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.SpaceBetween) {
            Caption("Progresso da rota: ${progress?.let { "${(it*100).toInt()}%" } ?: "Indisponível"}")
            Caption(remaining?.let { "${format(it)} km restantes" } ?: "Distância restante não verificada")
        }
        route?.progressSampledAt?.let { Caption("Posição na rota consultada ${dataAge(it, now)}") }
    }
    Row(horizontalArrangement=Arrangement.spacedBy(14.dp)) {
        PanelCard(Modifier.weight(1.3f).heightIn(min=330.dp)) {
            SectionTitle("Mapa da viagem",Icons.Outlined.Map)
            Box(Modifier.fillMaxWidth().weight(1f).heightIn(min=230.dp).background(androidx.compose.ui.graphics.Color(0xFF10191D)),contentAlignment=Alignment.Center) {
                if (route != null) JourneyRouteDrawing(route, state.gps)
                else Column(horizontalAlignment=Alignment.CenterHorizontally,verticalArrangement=Arrangement.spacedBy(14.dp)) {
                    Icon(Icons.Outlined.LocationOff,null,tint=Muted,modifier=Modifier.size(44.dp))
                    Text("Traçado indisponível")
                    Caption("Aguardando rota vinculada no canal autorizado da Cloud")
                    state.gps?.let {gps->
                        Text("Última posição recebida da Cloud",fontWeight=FontWeight.Bold)
                        Text("${format(gps.latitude,5)}, ${format(gps.longitude,5)}")
                        Caption("Captura: ${gps.sampledAt}")
                        Caption("Origem: ${gps.source} · ${gps.provider ?: "Provedor não informado"}")
                        Caption("Precisão informada: ${format(gps.accuracyM) ?: "—"} m · não verificada fisicamente")
                    } ?: Caption("Sem posição recebida da Cloud")
                }
            }
            if (route != null) {
                Caption("${format(route.distanceM / 1000, 1)} km no traçado${route.provider?.let { " · $it" }.orEmpty()}")
                Caption("Laranja · planejado${if (route.driven.isNotEmpty()) "   Branco · percorrido" else ""}")
                state.gps?.let { Caption("Última posição recebida · ${dataAge(it.sampledAt, now)} · ${it.source}") }
            }
        }
        Column(Modifier.weight(1f),verticalArrangement=Arrangement.spacedBy(12.dp)) {
            Row(horizontalArrangement=Arrangement.spacedBy(10.dp)) {
                Metric("Média da operação",format(trip?.consumption?.averageKml ?: localTrip?.averageKml,2),"km/l",Modifier.weight(1f),trip?.consumption?.fuel?.label(now) ?: localTrip?.label)
                Metric("Diesel consumido",format(trip?.consumption?.consumedLiters ?: localTrip?.litres),"L",Modifier.weight(1f))
            }
            Row(horizontalArrangement=Arrangement.spacedBy(10.dp)) {
                Metric("Distância da operação",format(trip?.consumption?.distanceKm ?: localTrip?.distanceKm),"km",Modifier.weight(1f))
                Metric("Duração planejada",route?.durationSeconds?.let(::duration),modifier=Modifier.weight(1f))
            }
            PanelCard(Modifier.fillMaxWidth()) {
                SectionTitle("Projeção até o destino")
                val next = state.routeForecast?.destination
                if (next != null) Caption("${format(next.distanceM / 1000)} km · ${format(next.liters)} L previstos")
                else Caption("Aguardando previsão topográfica da Cloud")
            }
        }
    }
    PanelCard(Modifier.fillMaxWidth()) {
        SectionTitle("Pedágios da operação",Icons.Outlined.PinDrop)
        if (route?.tolls.isNullOrEmpty()) Caption("Nenhuma praça informada para exibir.")
        else route?.tolls?.forEach { Text(it.name) }
    }
    if(state.financialAllowed) PanelCard(Modifier.fillMaxWidth().testTag("financial-card"),accent=true) {
        SectionTitle("Resultado da viagem",Icons.Outlined.AccountBalanceWallet)
        val f=state.financial
        if(f==null) Text("Dados financeiros indisponíveis") else {
            if(trip?.kind==LegKind.EMPTY) {
                Text("Custo do deslocamento vazio: ${money(f.incurredCents)}")
                Caption("Vínculo ao frete somente com confirmação no Motorista")
            } else {
                Text("Receita contratada: ${f.contractedCents?.let(::money) ?: "Indisponível"}")
                f.contractedCents?.let { Text("Sobra parcial: ${money(it-f.incurredCents)}");Text("Sobra operacional prevista na chegada: ${money(it-f.projectedCents)}") }
            }
            TextButton(onClick={showFinance=true}) {Text("Detalhar custos")}
        }
    }
    TextButton(onClick={operational=true}) { Text("Linha do tempo operacional") }
    if(operational) AlertDialog(onDismissRequest={operational=false},title={Text("Linha do tempo operacional")},text={Text("Eventos operacionais autorizados indisponíveis. Nenhum horário foi inferido.")},confirmButton={TextButton(onClick={operational=false}){Text("Fechar")}})
    if(showFinance && state.financialAllowed) AlertDialog(onDismissRequest={showFinance=false},title={Text("Resultado da viagem")},text={Text("Receita contratada não significa recebimento. Projeção operacional não equivale a lucro líquido. Detalhamento depende do contrato Cloud.")},confirmButton={TextButton(onClick={showFinance=false}){Text("Fechar")}})
    if(showJourney && state.journeyAllowed) JourneyDialog(state.journey,state.demo) {showJourney=false}
}

@Composable private fun JourneyRouteDrawing(route: JourneyRoute, gps: CloudPosition?) {
    val planned = route.points
    val extent = remember(route) { planned + route.driven.flatten() }
    val x0 = extent.minOf { it.longitude }
    val x1 = extent.maxOf { it.longitude }
    val y0 = extent.minOf { it.latitude }
    val y1 = extent.maxOf { it.latitude }
    val aspect = kotlin.math.cos(Math.toRadians((y0 + y1) / 2))
    Canvas(Modifier.fillMaxSize()) {
        val margin = 15.dp.toPx()
        val width = (x1 - x0) * aspect
        val height = y1 - y0
        val scale = minOf((size.width - 2 * margin) / width.coerceAtLeast(.00001),
            (size.height - 2 * margin) / height.coerceAtLeast(.00001)).coerceAtLeast(0.0)
        fun xy(latitude: Double, longitude: Double) = Offset(
            (size.width / 2 + (longitude - (x0 + x1) / 2) * aspect * scale).toFloat(),
            (size.height / 2 - (latitude - (y0 + y1) / 2) * scale).toFloat(),
        )
        fun line(points: List<RoutePosition>) = Path().apply {
            points.forEachIndexed { index, p ->
                val at = xy(p.latitude, p.longitude)
                if (index == 0) moveTo(at.x, at.y) else lineTo(at.x, at.y)
            }
        }
        drawPath(line(planned), Orange, style = RouteStroke(3.dp.toPx()))
        route.driven.forEach { drawPath(line(it), androidx.compose.ui.graphics.Color.White, style = RouteStroke(2.dp.toPx())) }
        route.tolls.forEach { toll ->
            toll.position?.let { position ->
                val at = xy(position.latitude, position.longitude)
                if (at.x in 0f..size.width && at.y in 0f..size.height) drawCircle(Muted, 3.dp.toPx(), at)
            }
        }
        val end = planned.last()
        drawCircle(Orange, 6.dp.toPx(), xy(end.latitude, end.longitude))
        gps?.let {
            val at = xy(it.latitude, it.longitude)
            if (at.x in 0f..size.width && at.y in 0f..size.height) drawCircle(androidx.compose.ui.graphics.Color.Cyan, 5.dp.toPx(), at)
        }
    }
}
private fun money(cents: Long) = java.text.NumberFormat.getCurrencyInstance(Locale.forLanguageTag("pt-BR")).format(cents/100.0)
private fun duration(seconds: Long) = "${seconds/3600}h${((seconds%3600)/60).toString().padStart(2,'0')}"
@Composable private fun JourneyDialog(data: JourneyView?,demo: Boolean,close: ()->Unit) {
    AlertDialog(onDismissRequest=close,modifier=Modifier.testTag("journey-dialog"),title={Text("Jornada · últimas 24 h")},text={
        Column(Modifier.verticalScroll(rememberScrollState()),verticalArrangement=Arrangement.spacedBy(12.dp)) {
            if(demo) Text("DEMONSTRAÇÃO · dados fictícios",color=Orange)
            if(data==null) Text("Dados de jornada indisponíveis") else {
                Caption("Janela móvel até ${data.reference}")
                val s=data.summary
                listOf("Direção" to s.driving,"Parado total (subtotal)" to s.stopped,"Descanso confirmado" to s.rest,"Espera / trabalho" to s.waiting,"Outras paradas" to s.otherStop,"Desconhecido" to s.unknown).forEach { (label,value) ->
                    Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.SpaceBetween){Text(label);Text(duration(value),fontWeight=FontWeight.Bold)}
                }
            }
            Caption("Veículo parado não confirma descanso legal. Classificações e correções vêm do Motorista. Critérios legais e próximo marco não verificados.")
        }
    },confirmButton={TextButton(onClick=close){Text("Fechar")}})
}
