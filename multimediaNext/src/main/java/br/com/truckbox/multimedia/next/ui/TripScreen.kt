package br.com.truckbox.multimedia.next.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
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
    PanelCard(Modifier.fillMaxWidth()) {
        Row(verticalAlignment=Alignment.CenterVertically,horizontalArrangement=Arrangement.spacedBy(18.dp)) {
            Column(Modifier.weight(1f),verticalArrangement=Arrangement.spacedBy(6.dp)) {
                Caption("OPERAÇÃO ATUAL · ${trip?.kind?.title ?: "NÃO VERIFICADA"}")
                Text(trip?.let{"${it.origin} → ${it.destination}"} ?: "Viagem atual não disponível",fontWeight=FontWeight.Bold,fontSize=22.sp)
                Caption(trip?.let{"${it.product ?: "Produto não informado"} · ${format(it.weightT) ?: "—"} t · cadastro no Motorista"} ?: "Aguardando vínculo e dados autorizados do Motorista/Cloud")
            }
            if(state.journeyAllowed) OutlinedButton(onClick={showJourney=true},modifier=Modifier.testTag("journey-button")) { Icon(Icons.Outlined.Schedule,null);Text("Jornada 24 h") }
        }
        val remaining=trip?.remainingKm
        val distance=trip?.consumption?.distanceKm
        val progress=if(remaining!=null && distance!=null && remaining>=0 && distance>=0 && remaining+distance>0) (distance/(distance+remaining)).toFloat() else null
        if(progress!=null) LinearProgressIndicator(progress={progress},modifier=Modifier.fillMaxWidth(),color=Orange,trackColor=Stroke)
        Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.SpaceBetween) {
            Caption("Progresso da rota: ${progress?.let { "${(it*100).toInt()}%" } ?: "Indisponível"}")
            Caption(remaining?.let { "${format(it)} km restantes" } ?: "Distância restante não verificada")
        }
    }
    Row(horizontalArrangement=Arrangement.spacedBy(14.dp)) {
        PanelCard(Modifier.weight(1.3f).heightIn(min=330.dp)) {
            SectionTitle("Mapa da viagem",Icons.Outlined.Map)
            Box(Modifier.fillMaxWidth().weight(1f).heightIn(min=230.dp).background(androidx.compose.ui.graphics.Color(0xFF10191D)),contentAlignment=Alignment.Center) {
                Column(horizontalAlignment=Alignment.CenterHorizontally,verticalArrangement=Arrangement.spacedBy(14.dp)) {
                    Icon(Icons.Outlined.LocationOff,null,tint=Muted,modifier=Modifier.size(44.dp))
                    Text("Mapa e posição indisponíveis")
                    Caption("Rota selecionada e GPS do Motorista não verificados")
                }
            }
            Caption("Sem projeção de localização. Última posição: não recebida.")
        }
        Column(Modifier.weight(1f),verticalArrangement=Arrangement.spacedBy(12.dp)) {
            Row(horizontalArrangement=Arrangement.spacedBy(10.dp)) {
                Metric("Média da operação",format(trip?.consumption?.averageKml,2),"km/l",Modifier.weight(1f))
                Metric("Diesel consumido",format(trip?.consumption?.consumedLiters),"L",Modifier.weight(1f))
            }
            Row(horizontalArrangement=Arrangement.spacedBy(10.dp)) {
                Metric("Distância da operação",format(trip?.consumption?.distanceKm),"km",Modifier.weight(1f))
                Metric("Previsão de chegada",null,modifier=Modifier.weight(1f),note="ETA não verificada")
            }
            PanelCard(Modifier.fillMaxWidth()) { SectionTitle("Projeção até o destino");Caption("Previsão indisponível · rota, posição e fonte não verificadas") }
        }
    }
    PanelCard(Modifier.fillMaxWidth()) {
        SectionTitle("Pedágios da operação",Icons.Outlined.PinDrop)
        Text("Passagens e praças não verificadas")
        Caption("Passagem identificada por GPS não comprova pagamento. Sem fonte validada de praças e tarifas.")
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
