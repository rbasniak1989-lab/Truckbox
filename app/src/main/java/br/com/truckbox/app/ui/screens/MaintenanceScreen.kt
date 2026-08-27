package br.com.truckbox.app.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.app.data.TruckState
import br.com.truckbox.app.domain.health.HealthItem
import br.com.truckbox.app.domain.health.HealthSeverity
import br.com.truckbox.app.operations.MaintenanceRecord
import br.com.truckbox.app.ui.theme.*
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

private val maintenanceTypes = listOf(
    "Óleo do motor", "Filtro de óleo", "Filtro de combustível", "Filtro de ar",
    "Regulagem de válvulas", "Óleo da caixa", "Óleo do diferencial", "Secador de ar"
)

@Composable
fun MaintenanceScreen(
    state: TruckState,
    records: List<MaintenanceRecord>,
    onAddMaintenance: (String, Double?, Double?, Double?, String) -> Unit,
) {
    var dialog by remember { mutableStateOf(false) }
    val currentKm = state.odometerKm.value

    Column(Modifier.fillMaxSize()) {
        TruckHeader(state)
        Column(Modifier.fillMaxSize().padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("MANUTENÇÃO E DIAGNÓSTICO", fontWeight = FontWeight.Black, fontSize = 20.sp)
                Spacer(Modifier.weight(1f))
                Button(onClick = { dialog = true }) { Icon(Icons.Default.Add, null); Spacer(Modifier.width(6.dp)); Text("NOVA MANUTENÇÃO") }
            }
            Row(Modifier.fillMaxWidth().weight(1f), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                TruckCard(Modifier.weight(0.46f).fillMaxHeight()) {
                    Text("MANUTENÇÕES PERIÓDICAS", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(9.dp))
                    Column(Modifier.verticalScroll(rememberScrollState())) {
                        maintenanceTypes.forEach { type ->
                            val last = records.firstOrNull { it.serviceType == type }
                            val remaining = if (currentKm != null && last?.nextDueKm != null) last.nextDueKm - currentKm else null
                            Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                                Column(Modifier.weight(1f)) {
                                    Text(type, fontWeight = FontWeight.Bold, fontSize = 12.sp)
                                    Text(
                                        last?.let { "feito ${it.odometerKm?.let { km -> fmt0(km) + " km" } ?: dateShort(it.performedAtMs)}" } ?: "não cadastrado",
                                        color = TruckMuted,
                                        fontSize = 9.sp,
                                    )
                                }
                                when {
                                    last?.nextDueKm == null -> Text("—", color = TruckMuted)
                                    remaining == null -> Text("${fmt0(last.nextDueKm)} km", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold, fontSize = 11.sp)
                                    remaining <= 0 -> Text("VENCIDA", color = StatusCritical, fontWeight = FontWeight.Black, fontSize = 11.sp)
                                    remaining <= 5000 -> Text("${fmt0(remaining)} km", color = StatusWarn, fontWeight = FontWeight.Black, fontSize = 11.sp)
                                    else -> Text("${fmt0(remaining)} km", color = StatusOk, fontWeight = FontWeight.Bold, fontSize = 11.sp)
                                }
                            }
                            HorizontalDivider(color = TruckBorder.copy(alpha = 0.45f))
                        }
                    }
                }

                TruckCard(Modifier.weight(0.29f).fillMaxHeight()) {
                    Text("HISTÓRICO", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(8.dp))
                    Column(Modifier.verticalScroll(rememberScrollState())) {
                        records.take(10).forEach { r ->
                            Column(Modifier.fillMaxWidth().padding(vertical = 5.dp)) {
                                Text(r.serviceType, fontWeight = FontWeight.Bold, fontSize = 11.sp)
                                Text(
                                    "${dateShort(r.performedAtMs)} • ${r.odometerKm?.let { fmt0(it) + " km" } ?: "km —"}${r.cost?.let { " • " + moneyMaint(it) } ?: ""}",
                                    color = TruckMuted,
                                    fontSize = 9.sp,
                                )
                            }
                            HorizontalDivider(color = TruckBorder.copy(alpha = 0.45f))
                        }
                        if (records.isEmpty()) Text("Nenhum serviço cadastrado ainda.", color = TruckMuted, fontSize = 11.sp)
                    }
                }

                Column(Modifier.weight(0.25f).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(9.dp)) {
                    TruckCard(Modifier.weight(1f).fillMaxWidth()) {
                        Text("SAÚDE DO CAMINHÃO", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(9.dp))
                        Text("Cálculos do Health Engine • toque em Sensores para detalhes", color = TruckMuted, fontSize = 9.sp)
                        Spacer(Modifier.height(7.dp))
                        HealthStatusLine(state.health.item("lubrication"))
                        HealthStatusLine(state.health.item("cooling"))
                        HealthStatusLine(state.health.item("intake"))
                        HealthStatusLine(state.health.item("fuel_supply"))
                        HealthStatusLine(state.health.item("trans_thermal"))
                        HealthStatusLine(state.health.item("clutch"))
                    }
                    TruckCard(Modifier.weight(1f).fillMaxWidth()) {
                        Text("DIAGNÓSTICO", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(9.dp))
                        Text("DTCs J1939: próxima etapa", fontWeight = FontWeight.Bold, fontSize = 11.sp)
                        Text("O Logger permanece disponível no Modo Desenvolvedor para investigação de falhas e sensores.", color = TruckMuted, fontSize = 10.sp)
                    }
                }
            }
        }
    }

    if (dialog) MaintenanceDialog(
        currentOdometerKm = currentKm,
        onDismiss = { dialog = false },
        onSave = { type, odo, next, cost, notes -> onAddMaintenance(type, odo, next, cost, notes); dialog = false },
    )
}

@Composable private fun HealthStatusLine(item: HealthItem?) {
    val severity = item?.severity ?: HealthSeverity.UNAVAILABLE
    val color = when (severity) {
        HealthSeverity.OK -> StatusOk
        HealthSeverity.INFO, HealthSeverity.LEARNING -> StatusInfo
        HealthSeverity.WARNING -> StatusWarn
        HealthSeverity.CRITICAL -> StatusCritical
        HealthSeverity.UNAVAILABLE -> TruckMuted
    }
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(item?.title ?: "Aguardando", fontSize = 9.sp, modifier = Modifier.weight(1f), maxLines = 1)
        Text(item?.headline ?: "sem sinal", color = color, fontWeight = FontWeight.Bold, fontSize = 8.sp, maxLines = 1)
    }
}

@Composable private fun MaintenanceDialog(
    currentOdometerKm: Double?,
    onDismiss: () -> Unit,
    onSave: (String, Double?, Double?, Double?, String) -> Unit,
) {
    var type by remember { mutableStateOf("Óleo do motor") }
    var odo by remember { mutableStateOf(currentOdometerKm?.let { fmt0(it) } ?: "") }
    var next by remember { mutableStateOf("") }
    var cost by remember { mutableStateOf("") }
    var notes by remember { mutableStateOf("") }
    var menu by remember { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Nova manutenção") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Box {
                    OutlinedButton(onClick = { menu = true }, modifier = Modifier.fillMaxWidth()) { Text(type); Spacer(Modifier.weight(1f)) }
                    DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                        maintenanceTypes.forEach { item -> DropdownMenuItem(text = { Text(item) }, onClick = { type = item; menu = false }) }
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    MaintDecimal(odo, { odo = it }, "KM atual", "km", Modifier.weight(1f))
                    MaintDecimal(next, { next = it }, "Próxima em", "km", Modifier.weight(1f))
                }
                MaintDecimal(cost, { cost = it }, "Custo", "R$", Modifier.fillMaxWidth())
                OutlinedTextField(notes, { notes = it }, label = { Text("Observações") }, modifier = Modifier.fillMaxWidth())
            }
        },
        confirmButton = { Button(onClick = { onSave(type, odo.n(), next.n(), cost.n(), notes) }) { Text("SALVAR") } },
        dismissButton = { OutlinedButton(onClick = onDismiss) { Text("CANCELAR") } },
    )
}

@Composable private fun MaintDecimal(value: String, onValueChange: (String) -> Unit, label: String, suffix: String, modifier: Modifier) {
    OutlinedTextField(value, { onValueChange(it.filter { c -> c.isDigit() || c == ',' || c == '.' }) }, label = { Text(label) }, suffix = { Text(suffix) }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = modifier)
}
private fun String.n(): Double? = replace(',', '.').toDoubleOrNull()
private fun dateShort(ms: Long): String = SimpleDateFormat("dd/MM/yy", Locale.getDefault()).format(Date(ms))
private fun moneyMaint(v: Double): String = String.format(Locale("pt", "BR"), "R$ %,.2f", v)
