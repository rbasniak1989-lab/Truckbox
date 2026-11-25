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
import br.com.truckbox.multimedia.next.data.canBeLastInstrumentValue
import kotlinx.coroutines.delay
import java.time.Instant

@Composable fun HomeScreen(state: AppState,now: Instant,modifier: Modifier=Modifier,onFaultsClick: ()->Unit = {}) {
    var detail by remember { mutableStateOf<Instrument?>(null) }
    Column(modifier,verticalArrangement=Arrangement.spacedBy(10.dp)) {
        Row(Modifier.weight(1f).fillMaxWidth().testTag("home-body"),horizontalArrangement=Arrangement.spacedBy(12.dp)) {
            HomeInstruments(state,now,{detail=it},Modifier.weight(2f).fillMaxHeight().testTag("home-instruments"))
            HomeIntelligence(state,now,onFaultsClick,Modifier.weight(1f).fillMaxHeight().testTag("home-intelligence"))
        }
        Surface(Modifier.fillMaxWidth().testTag("home-footer"),shape=androidx.compose.foundation.shape.RoundedCornerShape(13.dp),
            color=Panel,border=androidx.compose.foundation.BorderStroke(1.dp,Orange.copy(alpha=.55f))) {
            Row(Modifier.fillMaxWidth().padding(horizontal=16.dp,vertical=10.dp),horizontalArrangement=Arrangement.spacedBy(16.dp)) {
                Column(Modifier.weight(2.2f)) {
                    Caption("VIAGEM ATUAL")
                    Text(state.trip?.let { "${it.origin} → ${it.destination}" } ?: "Viagem não verificada",fontWeight=FontWeight.Bold,maxLines=1,overflow=androidx.compose.ui.text.style.TextOverflow.Ellipsis)
                }
                Column(Modifier.weight(1f)) { Caption("DISTÂNCIA");Text(state.trip?.consumption?.let {"${format(it.distanceKm)} km"} ?: "—",fontFamily=Oxanium,fontWeight=FontWeight.ExtraBold) }
                Column(Modifier.weight(1f)) { Caption("MÉDIA");Text(state.trip?.consumption?.averageKml?.let {"${format(it,2)} km/l"} ?: "—",fontFamily=Oxanium,fontWeight=FontWeight.ExtraBold) }
                Column(Modifier.weight(1f)) { Caption("SITUAÇÃO");Text(state.trip?.kind?.title ?: "Não verificada",fontWeight=FontWeight.SemiBold,maxLines=1,overflow=androidx.compose.ui.text.style.TextOverflow.Ellipsis) }
            }
        }
    }
    detail?.let { sensor -> SensorDialog(sensor,state.instrument(sensor),now,state.demo) { detail=null } }
}
@Composable private fun HomeInstruments(state: AppState,now: Instant,details: (Instrument)->Unit,modifier: Modifier) {
    Column(modifier,verticalArrangement=Arrangement.spacedBy(8.dp)) {
        Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.SpaceBetween) {
            Text("Instrumentos",fontWeight=FontWeight.Bold,fontSize=18.sp)
            Caption("13 cartões · 3 + 4 + 6")
        }
        val rows = listOf(Instrument.defaults.take(3),Instrument.defaults.subList(3,7),Instrument.defaults.drop(7))
        rows.forEachIndexed { size,sensors ->
            Row(Modifier.weight(1f).fillMaxWidth(),horizontalArrangement=Arrangement.spacedBy(8.dp)) {
                sensors.forEach { sensor ->
                    InstrumentCard(sensor,state.instrument(sensor),now,state.demo,size,Modifier.weight(1f).fillMaxHeight().testTag("instrument-${sensor.name}")) { details(sensor) }
                }
            }
        }
    }
}
@Composable private fun HomeIntelligence(state: AppState,now: Instant,onFaultsClick:()->Unit,modifier: Modifier) {
  BoxWithConstraints(modifier) {
    val compact=maxHeight<540.dp
    val tight=maxHeight<370.dp
    PanelCard(Modifier.fillMaxSize(),accent=true,contentPadding=if(compact) 10.dp else 16.dp,spacing=if(compact) 7.dp else 10.dp) {
        Text("TruckBox Intelligence",fontFamily=Inter,fontWeight=FontWeight.ExtraBold,fontSize=(if(tight) 15 else 19).sp,maxLines=1)
        val active=state.coreFaults.filter {it.pendingActive()}
        val confirmed=active.filter {it.confirmedActive(now,state.faultSnapshotAt,state.faultReadable)}
        val latest=active.maxByOrNull {it.lastSeen ?: Instant.MIN}
        val alertText=when {
            confirmed.isNotEmpty() -> "${confirmed.size} falha(s) ativa(s) reportada(s)"
            active.isNotEmpty() -> "${active.size} último(s) registro(s) ativo(s) · estado atual não confirmado"
            state.faultSnapshotAt==null -> "Aguardando diagnóstico da ECU"
            !state.faultReadable -> "Diagnóstico sem comunicação · estado desconhecido"
            else -> "Nenhuma falha ativa reportada · cobertura parcial"
        }
        PanelCard(Modifier.fillMaxWidth().weight(1f).clickable(onClick=onFaultsClick).testTag("home-fault-alert"),contentPadding=if(compact) 8.dp else 14.dp,spacing=if(compact) 4.dp else 8.dp) {
            Text("Monitoramento e alertas",fontWeight=FontWeight.Bold,fontSize=(if(compact) 12 else 16).sp,maxLines=1)
            Text(alertText,color=if(active.isNotEmpty()) Orange else Muted,fontWeight=FontWeight.Bold,fontSize=(if(compact) 11 else 15).sp,maxLines=if(tight) 2 else 2)
            if(latest!=null) Text(latest.technicalCode,color=Ink,fontSize=(if(compact) 10 else 12).sp,maxLines=1,overflow=androidx.compose.ui.text.style.TextOverflow.Ellipsis)
            if(!tight) Caption(if(latest!=null) "Toque para conferir código, estado e horários na Geral" else "Toque para abrir a Central de Falhas")
        }
        PanelCard(Modifier.fillMaxWidth().weight(1f),contentPadding=if(compact) 8.dp else 14.dp,spacing=if(compact) 4.dp else 8.dp) {
            Text("Inteligência preventiva",fontWeight=FontWeight.Bold,fontSize=(if(compact) 12 else 16).sp)
            Text(if(tight) "Dados insuficientes" else "Análise indisponível",fontWeight=FontWeight.Bold,fontSize=(if(compact) 12 else 15).sp)
            if(!tight) Text("Dados insuficientes. Evidências, cobertura e modelo ainda não verificados.",color=Muted,fontSize=(if(compact) 11 else 13).sp)
        }
        RotatingEfficiency(state,Modifier.weight(1.35f),compact)
    }
  }
}
@Composable fun RotatingEfficiency(state: AppState,modifier: Modifier=Modifier,compact: Boolean=false) {
    var slide by remember { mutableIntStateOf(0) }
    var paused by remember { mutableStateOf(false) }
    LaunchedEffect(paused) { if(!paused) while(true) { delay(14000);slide=(slide+1)%3 } }
    PanelCard(modifier.fillMaxWidth().clickable { paused=!paused },contentPadding=if(compact) 8.dp else 16.dp,spacing=if(compact) 4.dp else 10.dp) {
        Text("TruckBox Intelligence",fontWeight=FontWeight.Bold,fontSize=(if(compact) 12 else 16).sp)
        AnimatedContent(slide,transitionSpec={fadeIn(tween(500)) togetherWith fadeOut(tween(500))},label="Efficiency fade") { index ->
            Column(Modifier.heightIn(min=if(compact) 0.dp else 66.dp),verticalArrangement=Arrangement.spacedBy(if(compact) 3.dp else 6.dp)) {
                Text(listOf("Consumo da viagem","Desempenho","Projeções")[index],color=Orange,fontSize=(if(compact) 11 else 14).sp)
                val forecast = state.routeForecast
                val message = when (index) {
                    0 -> state.trip?.consumption?.averageKml?.let { "${format(it,2)} km/L" } ?: "Dados insuficientes"
                    1 -> forecast?.comparison?.differenceLiters?.let { delta ->
                        when {
                            delta > 0 -> "${format(delta,1)} L acima do previsto"
                            delta < 0 -> "${format(-delta,1)} L abaixo do previsto"
                            else -> "Consumo no valor previsto"
                        }
                    } ?: "Comparação por relevo indisponível"
                    else -> if ((forecast?.range?.deficitLiters ?: 0.0) > 0) "Abastecer para chegar com a reserva"
                        else forecast?.next50?.let { horizon ->
                            "${format(horizon.distanceM / 1_000,0)} km à frente · ${format(horizon.liters,1)} L previstos"
                        } ?: "Previsão topográfica indisponível"
                }
                Text(message,fontWeight=FontWeight.Bold,fontSize=(if(compact) 13 else 17).sp)
                if (index != 0 && forecast != null) {
                    val level = when(forecast.confidence) { "HIGH" -> "alta"; "MEDIUM" -> "média"; else -> "baixa" }
                    Caption("Confiança $level · ${dataAge(forecast.computedAt, Instant.now())}")
                }
            }
        }
        if(compact) Caption("${slide+1} de 3 · ${if(paused) "Pausado · toque para retomar" else "Toque para pausar"}")
        else Row(Modifier.fillMaxWidth(),horizontalArrangement=Arrangement.SpaceBetween) {
            TextButton(onClick={paused=!paused}) { Text(if(paused) "Retomar" else "Pausar") }
            TextButton(onClick={slide=(slide+1)%3;paused=true}) { Text("${slide+1} de 3  ›") }
        }
    }
}
@Composable fun SensorDialog(sensor: Instrument,value: InstrumentValue?,now: Instant,demo: Boolean,onClose: ()->Unit) {
    AlertDialog(onDismissRequest=onClose,title={Text(sensor.label)},text={
        Column(verticalArrangement=Arrangement.spacedBy(10.dp)) {
            val displayed = instrumentDisplayValue(sensor, value?.first, now, demo)
            val unit = value?.first?.unit ?: sensor.unit
            Text(displayed?.let { "${instrumentText(sensor, value?.first, it)} $unit".trim() } ?: "Sem leitura",fontWeight=FontWeight.Bold)
            if(!demo && displayed != null && value?.first?.canBeLastInstrumentValue()==true && value.first.observed(now)==null &&
                sensor !in listOf(Instrument.TORQUE, Instrument.GEAR)) Caption("Última leitura recebida · ${instrumentAge(value.first,now)} · desatualizada")
            value?.first?.takeIf { it.source==Origin.CORE && it.quality==Quality.UNVERIFIED && it.receivedAt!=null }?.value?.let {
                Caption("Valor recebido, não verificado: ${format(it)} ${sensor.unit}")
            }
            Caption("Origem: ${value?.first?.source?.name ?: "Não verificada"}")
            Caption("Sinal: ${value?.first?.sourceId ?: "Não informado"}")
            Caption("Captura: ${value?.first?.sampledAt ?: "Não informada"}")
            Caption("Recebimento: ${value?.first?.receivedAt ?: "Não informado"}")
            Caption("Qualidade: ${value?.first?.quality?.name ?: "MISSING"}")
            if(sensor==Instrument.TORQUE && value?.first?.validation=="DERIVADO_REFERENCIA_HISTORICA_EM_VALIDACAO")
                Caption("Percentual do PID 93 calculado com a referência histórica de 2.643 N·m, em validação.")
            value?.first?.sampleUptimeMs?.let { Caption("Amostra registrada aos ${it} ms do boot do Core") }
        }
    },confirmButton={TextButton(onClick=onClose){Text("Fechar")}})
}
