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
import java.time.ZoneId
import java.time.format.DateTimeFormatter

@Composable fun TruckScreen(state: AppState,now: Instant) {
    var tab by remember { mutableIntStateOf(0) }
    var period by remember { mutableIntStateOf(0) }
    var faultTab by remember { mutableIntStateOf(0) }
    var expanded by remember { mutableStateOf(false) }
    var selected by remember { mutableStateOf<Instrument?>(null) }
    var selectedFault by remember { mutableStateOf<EcuFault?>(null) }
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
                val rows=when(faultTab) {
                    0 -> state.coreFaults.filter {it.state==EcuFaultState.ACTIVE}
                    1 -> state.coreFaults.filter {it.state==EcuFaultState.INACTIVE}
                    else -> state.coreFaults
                }.sortedByDescending {it.lastSeen ?: Instant.MIN}
                Caption("Fonte: Core /api/diagnostics · J1708 PID 194 · cobertura parcial")
                if(!state.faultReadable) Caption("Estado atual não confirmado: sem resposta recente do diagnóstico.")
                if(state.faultIncomplete || state.faultOverflow>0) Caption("Cobertura incompleta: dados históricos podem faltar.")
                if(rows.isEmpty()) Text(if(state.faultSnapshotAt==null) "Aguardando consulta real da ECU" else
                    "Nenhum registro nesta categoria · não comprova ausência de falhas",color=Muted)
                rows.take(32).forEach { fault ->
                    Column(Modifier.fillMaxWidth()) {
                        TextButton(onClick={selectedFault=fault},modifier=Modifier.fillMaxWidth().testTag("fault-entry")) {
                            Column(Modifier.fillMaxWidth()) {
                                Text(fault.technicalCode,fontWeight=FontWeight.Bold,color=Orange)
                                Text(faultScreenStatus(fault,state,now),color=Muted)
                                Caption("Última observação: ${faultTime(fault.lastSeen)} · origem: ECU")
                            }
                        }
                        HorizontalDivider(color=Stroke.copy(alpha=.55f))
                    }
                }
                Caption("DTCs da ECU e alertas derivados são categorias distintas; não inferimos inativação por silêncio.")
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
                row.forEach { sensor -> InstrumentCard(sensor,state.instrument(sensor),now,state.demo,1,Modifier.weight(1f).height(158.dp)) { selected=sensor } }
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
    selected?.let { sensor -> SensorDialog(sensor,state.instrument(sensor),now,state.demo){selected=null} }
    selectedFault?.let {fault ->
        AlertDialog(onDismissRequest={selectedFault=null},
            title={Text(fault.technicalCode)},
            text={Column(verticalArrangement=Arrangement.spacedBy(8.dp)) {
                Text(faultScreenStatus(fault,state,now),color=Orange,fontWeight=FontWeight.Bold)
                Text("Origem: ECU J1708 · MID ${fault.mid}",fontWeight=FontWeight.SemiBold)
                Text("Descrição técnica OEM não validada. Não inferimos a causa mecânica.")
                Text("Primeira observação: ${faultTime(fault.firstSeen)}")
                Text("Última observação: ${faultTime(fault.lastSeen)}")
                Text("Ocorrências informadas: ${fault.occurrences?.toString() ?: "não disponíveis"}")
                Text("Qualidade: ${fault.quality.ifBlank {"não informada"}}")
                Caption("Ausência em novo quadro, perda de Wi-Fi ou desligamento não confirmam resolução.")
            }},confirmButton={TextButton(onClick={selectedFault=null}){Text("Fechar")}})
    }
}
@Composable private fun StatRow(sensor: String,min: String,mean: String,max: String) {
    Row(Modifier.fillMaxWidth().padding(vertical=8.dp),horizontalArrangement=Arrangement.spacedBy(8.dp)) {
        Text(sensor,Modifier.weight(2.3f))
        Text(min,Modifier.weight(1f));Text(mean,Modifier.weight(1f),color=Orange);Text(max,Modifier.weight(1f))
    }
}

private val faultClock=DateTimeFormatter.ofPattern("dd/MM HH:mm:ss").withZone(ZoneId.of("America/Sao_Paulo"))
private fun faultTime(t:Instant?):String=t?.let {faultClock.format(it)} ?: "horário não confirmado"
private fun faultScreenStatus(f:EcuFault,state:AppState,now:Instant):String=when(f.state) {
    EcuFaultState.ACTIVE -> if(f.confirmedActive(now,state.faultSnapshotAt,state.faultReadable))
        "Ativa reportada recentemente pela ECU" else "Último registro ativo · estado atual não confirmado"
    EcuFaultState.INACTIVE -> "Inativa reportada pela ECU"
    EcuFaultState.UNKNOWN -> "Estado não confirmado"
}
