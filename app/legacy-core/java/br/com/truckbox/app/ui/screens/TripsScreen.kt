package br.com.truckbox.app.ui.screens

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.LocalGasStation
import androidx.compose.material.icons.filled.Payments
import androidx.compose.material.icons.filled.StopCircle
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.app.data.OperationMode
import br.com.truckbox.app.data.TruckState
import br.com.truckbox.app.operations.OperationsSnapshot
import br.com.truckbox.app.ui.theme.*
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun TripsScreen(
    state: TruckState,
    operations: OperationsSnapshot,
    tankCapacityLiters: Double,
    tankRemainingLiters: Double?,
    onStartTrip: (String, String, Double?, Double?) -> Unit,
    onEndTrip: () -> Unit,
    onFueling: (Double, Double, String, String, Boolean) -> Unit,
    onExpense: (String, String, Double) -> Unit,
) {
    var tripDialog by remember { mutableStateOf(false) }
    var fuelingDialog by remember { mutableStateOf(false) }
    var expenseDialog by remember { mutableStateOf(false) }
    var confirmEnd by remember { mutableStateOf(false) }

    Column(Modifier.fillMaxSize()) {
        TruckHeader(state)
        Column(
            Modifier.fillMaxSize().padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(9.dp),
        ) {
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Button(onClick = { tripDialog = true }, enabled = operations.activeTrip == null) {
                    Icon(Icons.Default.Add, null); Spacer(Modifier.width(6.dp)); Text("NOVA VIAGEM")
                }
                OutlinedButton(onClick = { fuelingDialog = true }) {
                    Icon(Icons.Default.LocalGasStation, null); Spacer(Modifier.width(6.dp)); Text("ABASTECIMENTO")
                }
                OutlinedButton(onClick = { expenseDialog = true }) {
                    Icon(Icons.Default.Payments, null); Spacer(Modifier.width(6.dp)); Text("DESPESA")
                }
                if (operations.activeTrip != null) {
                    Button(onClick = { confirmEnd = true }) {
                        Icon(Icons.Default.StopCircle, null); Spacer(Modifier.width(6.dp)); Text("ENCERRAR VIAGEM")
                    }
                }
                Spacer(Modifier.weight(1f))
                AssistChip(
                    onClick = {},
                    label = {
                        Text(
                            when (operations.mode) {
                                OperationMode.LOADED_TRIP -> "CARREGADO"
                                OperationMode.EMPTY_MODE -> "MODO VAZIO"
                                OperationMode.NO_TRIP -> "SEM VIAGEM"
                            }
                        )
                    },
                )
            }

            Row(Modifier.fillMaxWidth().weight(1f), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                TruckCard(Modifier.weight(0.58f).fillMaxHeight()) {
                    Text("VIAGEM ATUAL", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(10.dp))
                    val trip = operations.activeTrip
                    if (trip != null) {
                        Text(
                            "${trip.origin}  →  ${trip.destination}",
                            fontWeight = FontWeight.Black,
                            fontStyle = FontStyle.Italic,
                            fontSize = 28.sp,
                        )
                        Spacer(Modifier.height(12.dp))
                        val currentOdo = state.odometerKm.value
                        val distance = if (trip.startOdometerKm != null && currentOdo != null) (currentOdo - trip.startOdometerKm).coerceAtLeast(0.0) else null
                        val fuel = (state.fuel.sessionLiters - trip.startFuelCounterL).coerceAtLeast(0.0)
                        val avg = if (distance != null && fuel > 0.02) distance / fuel else null
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                            TripMetric("DISTÂNCIA", distance?.let { fmt1(it) } ?: "—", "km", Modifier.weight(1f))
                            TripMetric("CONSUMIDO NA VIAGEM", fmt1(fuel), "L", Modifier.weight(1f))
                            TripMetric("MÉDIA GERAL", avg?.let { fmt2(it) } ?: "—", "km/L", Modifier.weight(1f))
                        }
                        Spacer(Modifier.height(12.dp))
                        Row(Modifier.fillMaxWidth()) {
                            InfoLine("Peso", trip.weightT?.let { "${fmt1(it)} t" } ?: "—", Modifier.weight(1f))
                            InfoLine("Valor/t", trip.ratePerT?.let { money(it) } ?: "—", Modifier.weight(1f))
                            InfoLine("Frete", trip.freightTotal?.let(::money) ?: "—", Modifier.weight(1f))
                        }
                        Spacer(Modifier.height(8.dp))
                        Text("Início: ${dateTime(trip.startedAtMs)}", color = TruckMuted, fontSize = 11.sp)
                    } else {
                        Text(
                            if (operations.mode == OperationMode.EMPTY_MODE) "Rodando vazio" else "Nenhuma viagem carregada ativa",
                            fontWeight = FontWeight.Black,
                            fontStyle = FontStyle.Italic,
                            fontSize = 28.sp,
                        )
                        Spacer(Modifier.height(10.dp))
                        Text(
                            if (operations.mode == OperationMode.EMPTY_MODE)
                                "O TruckBox mantém distância e combustível do trecho vazio até a próxima viagem carregada."
                            else "Toque em Nova viagem para cadastrar origem, destino, peso e valor por tonelada.",
                            color = TruckMuted,
                            fontSize = 12.sp,
                        )
                    }

                    Spacer(Modifier.weight(1f))
                    HorizontalDivider(color = TruckBorder)
                    Spacer(Modifier.height(8.dp))
                    Row(Modifier.fillMaxWidth()) {
                        InfoLine("Tanque", tankRemainingLiters?.let { "${fmt0(it)} L" } ?: "—", Modifier.weight(1f))
                        InfoLine("Capacidade", if (tankCapacityLiters > 0) "${fmt0(tankCapacityLiters)} L" else "—", Modifier.weight(1f))
                        InfoLine("Odômetro", state.odometerKm.value?.let { "${fmt0(it)} km" } ?: "—", Modifier.weight(1f))
                    }
                }

                Column(Modifier.weight(0.42f).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(9.dp)) {
                    TruckCard(Modifier.weight(0.55f).fillMaxWidth()) {
                        Text("HISTÓRICO DE VIAGENS", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(7.dp))
                        Column(Modifier.verticalScroll(rememberScrollState())) {
                            val history = operations.trips.filter { it.status == "COMPLETED" }.take(7)
                            if (history.isEmpty()) Text("Nenhuma viagem encerrada ainda.", color = TruckMuted, fontSize = 11.sp)
                            history.forEach { t ->
                                Row(Modifier.fillMaxWidth().padding(vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
                                    Column(Modifier.weight(1f)) {
                                        Text("${t.origin} → ${t.destination}", fontWeight = FontWeight.Bold, fontSize = 12.sp, maxLines = 1)
                                        Text(dateShort(t.startedAtMs), color = TruckMuted, fontSize = 9.sp)
                                    }
                                    Text(t.distanceKm?.let { "${fmt0(it)} km" } ?: "—", fontWeight = FontWeight.Bold, fontSize = 11.sp)
                                    Spacer(Modifier.width(10.dp))
                                    Text(t.freightTotal?.let(::money) ?: "—", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold, fontSize = 11.sp)
                                }
                                HorizontalDivider(color = TruckBorder.copy(alpha = 0.45f))
                            }
                        }
                    }
                    TruckCard(Modifier.weight(0.45f).fillMaxWidth()) {
                        Text("ÚLTIMOS ABASTECIMENTOS", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        operations.fuelings.take(5).forEach { f ->
                            Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                                Column(Modifier.weight(1f)) {
                                    Text(f.station.ifBlank { "Abastecimento" }, fontWeight = FontWeight.Bold, fontSize = 11.sp)
                                    Text(if (f.fullTank) "Tanque cheio • ${dateShort(f.tsMs)}" else dateShort(f.tsMs), color = TruckMuted, fontSize = 9.sp)
                                }
                                Text("${fmt0(f.liters)} L", fontWeight = FontWeight.Bold, fontSize = 11.sp)
                                Spacer(Modifier.width(8.dp))
                                Text(money(f.total), color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold, fontSize = 11.sp)
                            }
                        }
                        if (operations.fuelings.isEmpty()) Text("Nenhum abastecimento cadastrado.", color = TruckMuted, fontSize = 11.sp)
                    }
                }
            }
        }
    }

    if (tripDialog) NewTripDialog(
        onDismiss = { tripDialog = false },
        onSave = { o, d, w, r -> onStartTrip(o, d, w, r); tripDialog = false },
    )
    if (fuelingDialog) FuelingDialog(
        tankCapacityLiters = tankCapacityLiters,
        onDismiss = { fuelingDialog = false },
        onSave = { l, p, s, loc, full -> onFueling(l, p, s, loc, full); fuelingDialog = false },
    )
    if (expenseDialog) ExpenseDialog(
        onDismiss = { expenseDialog = false },
        onSave = { c, d, a -> onExpense(c, d, a); expenseDialog = false },
    )
    if (confirmEnd) AlertDialog(
        onDismissRequest = { confirmEnd = false },
        title = { Text("Encerrar viagem?") },
        text = { Text("O TruckBox fechará esta viagem e iniciará automaticamente o Modo Vazio.") },
        confirmButton = { Button(onClick = { onEndTrip(); confirmEnd = false }) { Text("ENCERRAR") } },
        dismissButton = { OutlinedButton(onClick = { confirmEnd = false }) { Text("CANCELAR") } },
    )
}

@Composable private fun TripMetric(title: String, value: String, unit: String, modifier: Modifier) {
    Surface(modifier, shape = MaterialTheme.shapes.medium, color = TruckSurface2, border = androidx.compose.foundation.BorderStroke(1.dp, TruckBorder)) {
        Column(Modifier.padding(10.dp)) {
            Text(title, color = TruckMuted, fontSize = 9.sp, fontWeight = FontWeight.Bold)
            Row(verticalAlignment = Alignment.Bottom) {
                Text(value, fontSize = 25.sp, fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic)
                Spacer(Modifier.width(5.dp)); Text(unit, color = MaterialTheme.colorScheme.primary, fontSize = 11.sp, modifier = Modifier.padding(bottom = 3.dp))
            }
        }
    }
}

@Composable private fun InfoLine(label: String, value: String, modifier: Modifier = Modifier) {
    Column(modifier) { Text(label.uppercase(), color = TruckMuted, fontSize = 9.sp); Text(value, fontWeight = FontWeight.Bold, fontSize = 13.sp) }
}

@Composable private fun NewTripDialog(onDismiss: () -> Unit, onSave: (String, String, Double?, Double?) -> Unit) {
    var origin by remember { mutableStateOf("") }; var destination by remember { mutableStateOf("") }
    var weight by remember { mutableStateOf("") }; var rate by remember { mutableStateOf("") }
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Nova viagem") }, text = {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(origin, { origin = it }, label = { Text("Origem") }, singleLine = true)
            OutlinedTextField(destination, { destination = it }, label = { Text("Destino") }, singleLine = true)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                DecimalField(weight, { weight = it }, "Peso", "t", Modifier.weight(1f))
                DecimalField(rate, { rate = it }, "Valor/t", "R$", Modifier.weight(1f))
            }
            val total = weight.numOrNull()?.let { w -> rate.numOrNull()?.let { r -> w * r } }
            Text("Frete total: ${total?.let(::money) ?: "—"}", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
        }
    }, confirmButton = { Button(onClick = { onSave(origin, destination, weight.numOrNull(), rate.numOrNull()) }, enabled = origin.isNotBlank() && destination.isNotBlank()) { Text("INICIAR") } }, dismissButton = { OutlinedButton(onClick = onDismiss) { Text("CANCELAR") } })
}

@Composable private fun FuelingDialog(tankCapacityLiters: Double, onDismiss: () -> Unit, onSave: (Double, Double, String, String, Boolean) -> Unit) {
    var liters by remember { mutableStateOf("") }; var price by remember { mutableStateOf("") }; var station by remember { mutableStateOf("") }; var location by remember { mutableStateOf("") }; var full by remember { mutableStateOf(false) }
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Novo abastecimento") }, text = {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                DecimalField(liters, { liters = it }, "Litros", "L", Modifier.weight(1f))
                DecimalField(price, { price = it }, "Preço/L", "R$", Modifier.weight(1f))
            }
            OutlinedTextField(station, { station = it }, label = { Text("Posto") }, singleLine = true)
            OutlinedTextField(location, { location = it }, label = { Text("Local") }, singleLine = true)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(full, { full = it }); Column { Text("Tanque cheio", fontWeight = FontWeight.Bold); if (tankCapacityLiters <= 0) Text("Configure a capacidade do tanque para calibrar o nível.", color = TruckMuted, fontSize = 9.sp) else Text("Ao marcar, o TruckBox calibra para ${fmt0(tankCapacityLiters)} L.", color = TruckMuted, fontSize = 9.sp) }
            }
            val total = liters.numOrNull()?.let { l -> price.numOrNull()?.let { p -> l * p } }
            Text("Total: ${total?.let(::money) ?: "—"}", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
        }
    }, confirmButton = { Button(onClick = { onSave(liters.numOrNull() ?: 0.0, price.numOrNull() ?: 0.0, station, location, full) }, enabled = (liters.numOrNull() ?: 0.0) > 0.0 && price.numOrNull() != null) { Text("SALVAR") } }, dismissButton = { OutlinedButton(onClick = onDismiss) { Text("CANCELAR") } })
}

@Composable private fun ExpenseDialog(onDismiss: () -> Unit, onSave: (String, String, Double) -> Unit) {
    var category by remember { mutableStateOf("Pedágio") }; var description by remember { mutableStateOf("") }; var amount by remember { mutableStateOf("") }
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Nova despesa") }, text = {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(category, { category = it }, label = { Text("Categoria") }, singleLine = true)
            OutlinedTextField(description, { description = it }, label = { Text("Descrição") }, singleLine = true)
            DecimalField(amount, { amount = it }, "Valor", "R$", Modifier.fillMaxWidth())
        }
    }, confirmButton = { Button(onClick = { onSave(category, description, amount.numOrNull() ?: 0.0) }, enabled = amount.numOrNull() != null) { Text("SALVAR") } }, dismissButton = { OutlinedButton(onClick = onDismiss) { Text("CANCELAR") } })
}

@Composable private fun DecimalField(value: String, onValueChange: (String) -> Unit, label: String, suffix: String, modifier: Modifier) {
    OutlinedTextField(value, { onValueChange(it.filter { c -> c.isDigit() || c == ',' || c == '.' }) }, label = { Text(label) }, suffix = { Text(suffix) }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = modifier)
}
private fun String.numOrNull(): Double? = replace(',', '.').toDoubleOrNull()
private fun money(v: Double): String = String.format(Locale("pt", "BR"), "R$ %,.2f", v)
private fun dateTime(ms: Long): String = SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.getDefault()).format(Date(ms))
private fun dateShort(ms: Long): String = SimpleDateFormat("dd/MM/yy", Locale.getDefault()).format(Date(ms))
