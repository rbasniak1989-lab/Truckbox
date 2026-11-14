package br.com.truckbox.multimedia.next.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import br.com.truckbox.multimedia.next.domain.*
import java.time.Instant

@Composable fun ConsumptionScreen(state: AppState,now: Instant) {
    var legIndex by remember(state.legs.map { it.kind }) {
        mutableIntStateOf(LegKind.entries.indexOfFirst { kind -> state.legs.any { it.kind==kind } }.coerceAtLeast(0))
    }
    var historyIndex by remember { mutableIntStateOf(1) }
    ChoiceRow(LegKind.entries.map { it.title },legIndex,{legIndex=it})
    val selected=LegKind.entries[legIndex]
    // Completa só existe se o backend fornecer essa agregação; nunca somar etapas implicitamente.
    val leg=state.legs.singleOrNull { it.kind==selected }
    val instant=instantConsumption(state.speed,state.fuelRate,now)
    Row(horizontalArrangement=Arrangement.spacedBy(12.dp)) {
        Metric("Média da viagem / etapa",format(leg?.averageKml,2),"km/l",Modifier.weight(1f))
        Metric("Últimos 100 km",format(state.last100Kml,2),"km/l",Modifier.weight(1f),"Janela técnica · não altera litros da etapa")
        Metric("Consumo instantâneo",format(instant?.value,2),instant?.unit ?: "km/l · L/h",Modifier.weight(1f))
    }
    Row(horizontalArrangement=Arrangement.spacedBy(10.dp)) {
        Metric("Litros consumidos da etapa",format(leg?.consumedLiters),"L",Modifier.weight(1f))
        Metric("Distância da etapa",format(leg?.distanceKm),"km",Modifier.weight(1f))
        Metric("Diesel estimado no tanque",format(state.tankLiters),"L",Modifier.weight(1f),state.tankStatus)
        Metric("Autonomia estimada",format(if(state.tankLiters!=null && state.last100Kml!=null) state.tankLiters*state.last100Kml else null,0),"km",Modifier.weight(1f),"Herda qualidade do saldo")
    }
    BoxWithConstraints {
        val panels: @Composable (Modifier,Modifier,Modifier)->Unit = { a,b,c ->
            PanelCard(a) {
                SectionTitle("Evolução do consumo")
                ChoiceRow(listOf("50 km","100 km","500 km","Viagem"),historyIndex,{historyIndex=it})
                Spacer(Modifier.height(40.dp))
                Text("Dados insuficientes")
                Caption("Série da etapa e janela selecionadas ainda indisponível. Sem curva ilustrativa no modo real.")
            }
            PanelCard(b) {
                SectionTitle("Previsão da rota")
                listOf("Próximos 50 km","Próximos 100 km","Até o destino").forEach {
                    Text(it);Caption("Previsão indisponível");HorizontalDivider(color=Stroke)
                }
                Caption("Fonte e confiança não verificadas. GPS não fornece relevo futuro.")
            }
            RotatingEfficiency(state,c)
        }
        if(maxWidth>=1150.dp) Row(horizontalArrangement=Arrangement.spacedBy(12.dp)) { panels(Modifier.weight(1.2f),Modifier.weight(1f),Modifier.weight(1f)) }
        else Column(verticalArrangement=Arrangement.spacedBy(12.dp)) { panels(Modifier.fillMaxWidth(),Modifier.fillMaxWidth(),Modifier.fillMaxWidth()) }
    }
}
