package br.com.truckbox.multimedia.next.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import br.com.truckbox.multimedia.next.domain.*
import java.time.Instant

@Composable fun TruckScreen(state: AppState,now: Instant) {
    var tab by remember { mutableIntStateOf(0) }
    var period by remember { mutableIntStateOf(0) }
    var faultTab by remember { mutableIntStateOf(0) }
    var expanded by remember { mutableStateOf(false) }
    var selected by remember { mutableStateOf<Instrument?>(null) }
    val system=TruckSystem.entries[tab]
    ChoiceRow(TruckSystem.entries.map { it.title },tab,{tab=it;expanded=false},Modifier.horizontalScroll(rememberScrollState()).testTag("system-tabs"))
    PanelCard(Modifier.fillMaxWidth().testTag("intelligence-first"),accent=true) {
        Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.SpaceBetween) {
            SectionTitle("TruckBox Intelligence")
            ChoiceRow(AnalysisPeriod.entries.map { it.title },period,{period=it})
        }
        Text("Análise indisponível",fontWeight=FontWeight.Bold)
        Caption("${system.title} · ${AnalysisPeriod.entries[period].title} · Dados insuficientes")
        Caption("Cobertura, origem, evidências e modelo não verificados. Nenhum diagnóstico mecânico foi inferido.")
    }
    if(system==TruckSystem.GENERAL) {
        Row(horizontalArrangement=Arrangement.spacedBy(14.dp)) {
            PanelCard(Modifier.weight(1.4f).testTag("fault-central")) {
                SectionTitle("Falhas do caminhão",Icons.Outlined.WarningAmber)
                ChoiceRow(listOf("Ativas","Inativas","Histórico"),faultTab,{faultTab=it})
                Text("Captura DTC indisponível")
                Caption("Códigos, episódios e horários ainda não verificados na ECU. A perda de comunicação não confirma inativação.")
                Caption("DTC da ECU e anomalia derivada terão origens distintas. Nenhum código de protótipo é usado.")
            }
            PanelCard(Modifier.weight(1f)) {
                SectionTitle("Resumo dos sistemas",Icons.Outlined.GridView)
                TruckSystem.entries.drop(1).forEachIndexed { i,s ->
                    TextButton(onClick={tab=i+1}){Text(s.title);Spacer(Modifier.weight(1f));Caption("Não verificado ›")}
                }
            }
        }
    } else {
        SectionTitle("Sensores principais · ${system.title}",Icons.Outlined.Sensors)
        system.sensors.chunked(4).forEach { row ->
            Row(horizontalArrangement=Arrangement.spacedBy(10.dp)) {
                row.forEach { sensor -> InstrumentCard(sensor,state.readings[sensor],now,state.demo,1,Modifier.weight(1f).height(158.dp)) { selected=sensor } }
                repeat(4-row.size){Spacer(Modifier.weight(1f))}
            }
        }
        val extraLabels=when(system) {
            TruckSystem.ENGINE -> listOf("Rotação do motor · rpm","Carga do motor · %","Vazão de diesel · L/h")
            TruckSystem.TRANSMISSION -> listOf("Modo i-Shift","I-Roll","VEB · interação")
            TruckSystem.BRAKES -> listOf("ABS","Freio de serviço","VEB+")
            else -> emptyList()
        }
        if(extraLabels.isNotEmpty()) Row(horizontalArrangement=Arrangement.spacedBy(10.dp)) {
            extraLabels.forEach { label -> Metric(label,null,modifier=Modifier.weight(1f)) }
        }
    }
    PanelCard(Modifier.fillMaxWidth().testTag("statistics")) {
        SectionTitle("Mínimo · Média · Máximo",Icons.Outlined.Analytics)
        Caption("${AnalysisPeriod.entries[period].title} · Média ponderada no tempo; lacunas excluídas")
        StatRow("Sensor","Mínimo","Média","Máximo")
        HorizontalDivider(color=Stroke)
        system.sensors.forEach { sensor ->
            val start=AnalysisPeriod.entries[period].seconds?.let {now.minusSeconds(it)} ?: state.trip?.legStartedAt
            val stats=if(start!=null && sensor !in listOf(Instrument.GEAR,Instrument.AIR)) timeStats(state.series[sensor].orEmpty(),start,now) else null
            StatRow("${sensor.label} ${sensor.unit}",format(stats?.min) ?: "—",format(stats?.mean) ?: "—",format(stats?.max) ?: "—")
            HorizontalDivider(color=Stroke.copy(alpha=.6f))
        }
        Caption("Sem cobertura suficiente: Dados insuficientes. Sinais categóricos não recebem média numérica; circuitos de ar exigem séries independentes.")
    }
    if(system!=TruckSystem.GENERAL) {
        OutlinedButton(onClick={expanded=!expanded},modifier=Modifier.testTag("expand-sensors")){Text(if(expanded) "Recolher sensores" else "Ver todos os sensores")}
        if(expanded) PanelCard(Modifier.fillMaxWidth()) {
            Text("Sinais adicionais",fontWeight=FontWeight.Bold)
            Caption("Catálogo expandido e proveniência por ECU aguardam validação. Nenhum sinal extra foi inventado.")
        }
    }
    selected?.let { sensor -> SensorDialog(sensor,state.readings[sensor],now,state.demo){selected=null} }
}
@Composable private fun StatRow(sensor: String,min: String,mean: String,max: String) {
    Row(Modifier.fillMaxWidth().padding(vertical=8.dp),horizontalArrangement=Arrangement.spacedBy(8.dp)) {
        Text(sensor,Modifier.weight(2.3f))
        Text(min,Modifier.weight(1f));Text(mean,Modifier.weight(1f),color=Orange);Text(max,Modifier.weight(1f))
    }
}
