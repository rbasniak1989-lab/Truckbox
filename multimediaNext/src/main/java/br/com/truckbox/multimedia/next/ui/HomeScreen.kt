package br.com.truckbox.multimedia.next.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.clickable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.multimedia.next.domain.*
import kotlinx.coroutines.delay
import java.time.Instant

@Composable fun HomeScreen(state: AppState,now: Instant) {
    var detail by remember { mutableStateOf<Instrument?>(null) }
    val intelligenceHeight=(homeHeightBudget()+50).coerceAtLeast(640f).dp
    BoxWithConstraints(Modifier.fillMaxWidth()) {
        if(maxWidth>=900.dp) {
            Row(horizontalArrangement=Arrangement.spacedBy(16.dp)) {
                HomeInstruments(state,now,{detail=it},Modifier.weight(2.1f))
                HomeIntelligence(state,Modifier.weight(1f).height(intelligenceHeight))
            }
        } else Column(verticalArrangement=Arrangement.spacedBy(14.dp)) {
            HomeInstruments(state,now,{detail=it},Modifier.fillMaxWidth())
            HomeIntelligence(state,Modifier.fillMaxWidth().height(intelligenceHeight))
        }
    }
    PanelCard(Modifier.fillMaxWidth(),accent=true) {
        Caption("VIAGEM ATUAL")
        Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.spacedBy(28.dp)) {
            Text(state.trip?.let { "${it.origin} → ${it.destination}" } ?: "Viagem não verificada",Modifier.weight(1f),fontWeight=FontWeight.Bold)
            Caption(state.trip?.consumption?.let { "${format(it.distanceKm)} km · ${format(it.averageKml,2) ?: "—"} km/l" } ?: "Distância e média indisponíveis")
        }
    }
    detail?.let { sensor -> SensorDialog(sensor,state.readings[sensor],now,state.demo) { detail=null } }
}
@Composable private fun HomeInstruments(state: AppState,now: Instant,details: (Instrument)->Unit,modifier: Modifier) {
    val heightBudget=homeHeightBudget()
    Column(modifier,verticalArrangement=Arrangement.spacedBy(10.dp)) {
        Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.SpaceBetween) {
            Text("Instrumentos",fontWeight=FontWeight.Bold,fontSize=18.sp)
            Caption("13 cartões · 3 + 4 + 6")
        }
        val rows = listOf(Instrument.defaults.take(3),Instrument.defaults.subList(3,7),Instrument.defaults.drop(7))
        rows.forEachIndexed { size,sensors ->
            Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.spacedBy(9.dp)) {
                sensors.forEach { sensor ->
                    InstrumentCard(sensor,state.readings[sensor],now,state.demo,size,Modifier.weight(1f).height((heightBudget*when(size){0->.35f;1->.33f;else->.32f}).dp).testTag("instrument-${sensor.name}")) { details(sensor) }
                }
            }
        }
    }
}
@Composable private fun HomeIntelligence(state: AppState,modifier: Modifier) {
    PanelCard(modifier,accent=true) {
        SectionTitle("TruckBox Intelligence")
        PanelCard(Modifier.fillMaxWidth().weight(1f)) {
            SectionTitle("Monitoramento e alertas",Icons.Outlined.NotificationsActive)
            Text("Não verificado",color=Muted,fontWeight=FontWeight.Bold)
            Caption("Sem captura DTC confirmada. Ausência de leitura não comprova operação normal.")
        }
        PanelCard(Modifier.fillMaxWidth().weight(1f)) {
            SectionTitle("Inteligência preventiva",Icons.Outlined.Insights)
            Text("Análise indisponível",fontWeight=FontWeight.Bold)
            Caption("Dados insuficientes. Evidências, cobertura e modelo ainda não verificados.")
        }
        RotatingEfficiency(state,Modifier.weight(1.35f))
    }
}
@Composable private fun homeHeightBudget(): Float {
    val size=LocalWindowInfo.current.containerSize
    return (with(LocalDensity.current){size.height.toDp().value}-270).coerceIn(480f,750f)
}
@Composable fun RotatingEfficiency(state: AppState,modifier: Modifier=Modifier) {
    var slide by remember { mutableIntStateOf(0) }
    var paused by remember { mutableStateOf(false) }
    LaunchedEffect(paused) { if(!paused) while(true) { delay(14000);slide=(slide+1)%3 } }
    PanelCard(modifier.fillMaxWidth().clickable { paused=true }) {
        SectionTitle("Eficiência e previsões",Icons.Outlined.BarChart)
        AnimatedContent(slide,transitionSpec={fadeIn(tween(500)) togetherWith fadeOut(tween(500))},label="Efficiency fade") { index ->
            Column(Modifier.heightIn(min=66.dp),verticalArrangement=Arrangement.spacedBy(6.dp)) {
                Text(listOf("Consumo da viagem","Desempenho","Projeções")[index],color=Orange)
                Text(if(index==0) state.trip?.consumption?.averageKml?.let { "${format(it,2)} km/l" } ?: "Dados insuficientes" else if(index==1) "Análise indisponível" else "Previsão indisponível",fontWeight=FontWeight.Bold)
            }
        }
        Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.SpaceBetween) {
            TextButton(onClick={paused=!paused}) { Text(if(paused) "Retomar" else "Pausar") }
            TextButton(onClick={slide=(slide+1)%3;paused=true}) { Text("${slide+1} de 3  ›") }
        }
    }
}
@Composable fun SensorDialog(sensor: Instrument,value: InstrumentValue?,now: Instant,demo: Boolean,onClose: ()->Unit) {
    AlertDialog(onDismissRequest=onClose,title={Text(sensor.label)},text={
        Column(verticalArrangement=Arrangement.spacedBy(10.dp)) {
            Text(visibleValue(value?.first,now,demo)?.let { "${format(it)} ${sensor.unit}" } ?: "Sem leitura",fontWeight=FontWeight.Bold)
            Caption("Origem: ${value?.first?.source?.name ?: "Não verificada"}")
            Caption("Captura: ${value?.first?.sampledAt ?: "Não informada"}")
            Caption("Recebimento: ${value?.first?.receivedAt ?: "Não informado"}")
            Caption("Qualidade: ${value?.first?.quality?.name ?: "MISSING"}")
            Caption("Uma resposta de rede não valida a leitura da ECU. Valores antigos não são apresentados como atuais.")
        }
    },confirmButton={TextButton(onClick=onClose){Text("Fechar")}})
}
