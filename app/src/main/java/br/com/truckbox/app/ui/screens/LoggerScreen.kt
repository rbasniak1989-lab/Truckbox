package br.com.truckbox.app.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.app.data.TruckState
import br.com.truckbox.app.ui.theme.*

@Composable
fun LoggerScreen(state: TruckState, onSaveOccurrence: () -> Unit) {
    val c = state.connection
    Column(Modifier.fillMaxSize()) {
        TruckHeader(state)
        Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(Modifier.weight(0.42f), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                TruckCard(Modifier.weight(0.30f)) {
                    Text("COMUNICAÇÃO CAN", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(12.dp))
                    LogMetric("Wi-Fi", if (c.wifiBound) "OK" else "OFF")
                    LogMetric("TCP 35000", if (c.tcpConnected) "CONECTADO" else "DESCONECTADO")
                    LogMetric("CAN", if (c.canActive) "ATIVO" else "SEM FRAMES")
                    LogMetric("Frames/s", c.framesPerSecond.toInt().toString())
                    LogMetric("Frames total", c.totalFrames.toString())
                    LogMetric("Linhas inválidas", c.invalidLines.toString())
                    LogMetric("Reconexões", c.reconnects.toString())
                }
                TruckCard(Modifier.weight(0.42f)) {
                    Text("MONITOR DE PGNs", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(8.dp))
                    val top = state.logger.pgnCounts.entries.sortedByDescending { it.value }.take(8)
                    if (top.isEmpty()) Text("Aguardando frames...", color = TruckMuted)
                    top.forEach { (pgn, count) ->
                        Row(Modifier.fillMaxWidth().padding(vertical = 3.dp)) {
                            Text("PGN $pgn", Modifier.weight(1f), fontWeight = FontWeight.Bold)
                            Text(count.toString(), color = TruckMuted)
                        }
                    }
                }
                TruckCard(Modifier.weight(0.28f)) {
                    Text("GPS / SISTEMA", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(12.dp))
                    LogMetric("GPS", if (state.gps.hasFix) "FIX" else "SEM FIX")
                    LogMetric("Satélites", state.gps.satellites.toString())
                    LogMetric("Buffer RAW", "${state.logger.ringBufferFrames} frames")
                    c.lastFrameAgeMs?.let { LogMetric("Último CAN", "${it} ms") }
                    Spacer(Modifier.height(6.dp))
                    Text("I-SHIFT PREDICT", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold, fontSize = 10.sp)
                    val pred = state.transmission.prediction
                    LogMetric("Fase", pred.phase)
                    LogMetric("Candidato", pred.predictedGear?.toString() ?: "—")
                    LogMetric("Score", "${(pred.score * 100).toInt()}%")
                    LogMetric("dRPM/s", "%.1f".format(pred.rpmSlopePerSecond))
                    pred.estimatedSecondsToLowRpm?.let { LogMetric("Janela", "%.1f s".format(it)) }
                    c.lastError?.let { Spacer(Modifier.height(8.dp)); Text(it, color = StatusWarn, fontSize = 10.sp) }
                }
            }
            Row(Modifier.weight(0.58f), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                TruckCard(Modifier.weight(0.68f)) {
                    Text("ÚLTIMOS FRAMES RAW", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(8.dp))
                    state.logger.recentFrames.forEach { f ->
                        Text("${f.timestampMs}  PGN ${f.pgn}  SA ${f.sa}  ${f.dataHex}", color = TruckMuted, fontSize = 10.sp, fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace, maxLines = 1)
                    }
                }
                TruckCard(Modifier.weight(0.32f), accentBorder = true) {
                    Text("MARCAR OCORRÊNCIA", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(10.dp))
                    Text("Salva o buffer RAW recente para analisarmos previsão de marcha, consumo, sensor ou qualquer comportamento estranho.", color = TruckMuted, fontSize = 11.sp)
                    Spacer(Modifier.weight(1f))
                    Button(onClick = onSaveOccurrence, modifier = Modifier.fillMaxWidth(), colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.primary)) {
                        Text("Salvar últimos ~2 min", fontWeight = FontWeight.Bold)
                    }
                    state.logger.lastSavedFile?.let {
                        Spacer(Modifier.height(8.dp)); Text("Salvo em:\n$it", color = StatusOk, fontSize = 9.sp, maxLines = 3)
                    }
                }
            }
        }
    }
}

@Composable private fun LogMetric(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) { Text(label, color = TruckMuted, fontSize = 11.sp); Spacer(Modifier.weight(1f)); Text(value, fontWeight = FontWeight.Bold, fontStyle = FontStyle.Italic, fontSize = 12.sp) }
}
