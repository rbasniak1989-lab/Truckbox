package br.com.truckbox.app.ui.screens

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.app.data.TruckState
import br.com.truckbox.app.preferences.AccentOption
import br.com.truckbox.app.ui.theme.*
import java.util.Locale

@Composable
fun SettingsScreen(
    state: TruckState,
    accent: AccentOption,
    developerMode: Boolean,
    tankCapacityLiters: Double,
    tankRemainingLiters: Double?,
    coreHost: String,
    cloudDeviceUid: String,
    cloudConfigured: Boolean,
    onAccentChanged: (AccentOption) -> Unit,
    onDeveloperModeChanged: (Boolean) -> Unit,
    onTankCapacityChanged: (Double) -> Unit,
    onTankManualCalibration: (Double) -> Unit,
    onCoreHostChanged: (String) -> Unit,
    onCloudConfigChanged: (String, String) -> Unit,
) {
    var capacityText by remember(tankCapacityLiters) {
        mutableStateOf(if (tankCapacityLiters > 0.0) String.format(Locale.US, "%.0f", tankCapacityLiters) else "")
    }
    var currentFuelText by remember(tankRemainingLiters) { mutableStateOf("") }
    var connectionDialog by remember { mutableStateOf(false) }

    Column(Modifier.fillMaxSize()) {
        TruckHeader(state)
        Row(Modifier.fillMaxSize().padding(14.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Column(Modifier.weight(0.43f), verticalArrangement = Arrangement.spacedBy(9.dp)) {
                SettingRow(
                    "Caminhão",
                    if (tankCapacityLiters > 0) "Volvo FH 540 • tanque ${fmt0(tankCapacityLiters)} L" else "Volvo FH 540 • capacidade do tanque não configurada",
                    Modifier.weight(1f),
                )
                SettingRow(
                    "Dispositivos e conexão",
                    "TruckBox Core + Starlink • GPS da multimídia • TOQUE PARA CONFIGURAR",
                    Modifier.weight(1f).clickable { connectionDialog = true },
                )
                SettingRow("Consumo e calibração", "TruckBox é a fonte oficial • autonomia pelos últimos 100 km", Modifier.weight(1f))
                SettingRow("Tela e funcionamento", "Modo escuro • paisagem • 13\" • inicialização automática", Modifier.weight(1f))
                SettingRow("Alertas e automações", "Viagem esquecida • manutenção • anomalias", Modifier.weight(1f))
                SettingRow("Conta, frota e permissões", "Cloud por caminhão/dispositivo", Modifier.weight(1f))
            }

            Column(Modifier.weight(0.57f), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                TruckCard(Modifier.weight(1.18f)) {
                    Text("COMBUSTÍVEL / TANQUE", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(7.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.Bottom) {
                        OutlinedTextField(
                            value = capacityText,
                            onValueChange = { capacityText = it.filter { c -> c.isDigit() || c == '.' || c == ',' } },
                            label = { Text("Capacidade total") },
                            suffix = { Text("L") },
                            singleLine = true,
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                            modifier = Modifier.weight(0.44f),
                        )
                        Button(
                            onClick = {
                                capacityText.replace(',', '.').toDoubleOrNull()?.takeIf { it > 0.0 }?.let(onTankCapacityChanged)
                            },
                            modifier = Modifier.height(56.dp),
                        ) { Text("SALVAR") }
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(
                        when {
                            tankCapacityLiters <= 0.0 -> "Informe a capacidade somada dos tanques do caminhão."
                            tankRemainingLiters == null -> "Capacidade salva: ${fmt0(tankCapacityLiters)} L. Marque 'Tanque cheio' no próximo abastecimento para iniciar a estimativa automática."
                            else -> "Estimativa atual: ${fmt0(tankRemainingLiters)} L de ${fmt0(tankCapacityLiters)} L. O TruckBox desconta automaticamente o combustível calculado."
                        },
                        color = TruckMuted,
                        fontSize = 10.sp,
                    )
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.Bottom) {
                        OutlinedTextField(
                            value = currentFuelText,
                            onValueChange = { currentFuelText = it.filter { c -> c.isDigit() || c == '.' || c == ',' } },
                            label = { Text("Calibração manual opcional") },
                            suffix = { Text("L atuais") },
                            singleLine = true,
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
                            modifier = Modifier.weight(1f),
                        )
                        OutlinedButton(
                            onClick = {
                                currentFuelText.replace(',', '.').toDoubleOrNull()?.let(onTankManualCalibration)
                                currentFuelText = ""
                            },
                            enabled = tankCapacityLiters > 0.0,
                            modifier = Modifier.height(56.dp),
                        ) { Text("AJUSTAR") }
                    }
                }

                Row(Modifier.weight(1f), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    TruckCard(Modifier.weight(1f).fillMaxHeight()) {
                        Text("APARÊNCIA", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(8.dp))
                        Text("Tema escuro fixo", fontWeight = FontWeight.Bold)
                        Text("Cor de destaque por caminhão; alertas mantêm cores semânticas.", color = TruckMuted, fontSize = 10.sp)
                        Spacer(Modifier.height(12.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(9.dp)) {
                            AccentOption.entries.forEach { option ->
                                val selected = option == accent
                                Surface(
                                    modifier = Modifier.size(if (selected) 34.dp else 30.dp).clickable { onAccentChanged(option) },
                                    shape = CircleShape,
                                    color = Color(option.argb),
                                    border = if (selected) BorderStroke(3.dp, Color.White) else BorderStroke(1.dp, TruckBorder),
                                ) { }
                            }
                        }
                        Spacer(Modifier.height(7.dp))
                        Text("Cor atual: ${accent.label}", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                    }

                    TruckCard(Modifier.weight(1f).fillMaxHeight()) {
                        Text("MODO DO SOFTWARE", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(8.dp))
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Text("Modo Desenvolvedor", fontWeight = FontWeight.Bold)
                                Text("Exibe Logger e ferramentas técnicas.", color = TruckMuted, fontSize = 10.sp)
                            }
                            Switch(checked = developerMode, onCheckedChange = onDeveloperModeChanged)
                        }
                        Spacer(Modifier.height(8.dp))
                        Text("TruckBox v0.4.1 Cloud", fontWeight = FontWeight.Bold)
                        val c = state.connection
                        Text(
                            "TCP ${if (c.tcpConnected) "OK" else "OFF"} • CAN ${if (c.canActive) "OK" else "OFF"} • GPS externo ${if (state.gps.hasFix) "OK" else "—"}",
                            color = TruckMuted,
                            fontSize = 10.sp,
                        )
                        Text("Core: $coreHost", color = TruckMuted, fontSize = 9.sp, maxLines = 1)
                        Text("Cloud: ${if (cloudConfigured) "CONFIGURADO" else "SEM TOKEN"}", color = if (cloudConfigured) StatusOk else TruckMuted, fontSize = 9.sp, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.height(6.dp))
                        OutlinedButton(onClick = { connectionDialog = true }, modifier = Modifier.fillMaxWidth()) { Text("CONFIGURAR CONEXÃO", fontSize = 10.sp) }
                    }
                }
            }
        }
    }

    if (connectionDialog) {
        ConnectionDialog(
            currentHost = coreHost,
            currentUid = cloudDeviceUid,
            tokenAlreadyConfigured = cloudConfigured,
            onDismiss = { connectionDialog = false },
            onSave = { host, uid, token ->
                onCoreHostChanged(host)
                onCloudConfigChanged(uid, token)
                connectionDialog = false
            },
        )
    }
}

@Composable
private fun ConnectionDialog(
    currentHost: String,
    currentUid: String,
    tokenAlreadyConfigured: Boolean,
    onDismiss: () -> Unit,
    onSave: (String, String, String) -> Unit,
) {
    var host by remember { mutableStateOf(currentHost) }
    var uid by remember { mutableStateOf(currentUid) }
    var token by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Conexão TruckBox") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(host, { host = it }, label = { Text("Host do Core") }, supportingText = { Text("Na Starlink atual, use 192.168.1.145") }, singleLine = true)
                OutlinedTextField(uid, { uid = it }, label = { Text("Device UID Cloud") }, singleLine = true)
                OutlinedTextField(token, { token = it }, label = { Text(if (tokenAlreadyConfigured) "Novo token (opcional)" else "Device Token (opcional para conexão local)") }, singleLine = true)
                if (!tokenAlreadyConfigured && token.isBlank()) {
                    Text("Pode salvar sem token: o Core local funciona normalmente. A sincronização Cloud feita pelo app ficará desativada até informar um token válido.", color = TruckMuted, fontSize = 9.sp)
                }
                Text("A multimídia e o Core ficam na mesma Starlink. O app usa o Core local para tempo real e a nuvem para sincronização operacional.", color = TruckMuted, fontSize = 10.sp)
            }
        },
        confirmButton = { Button(onClick = { onSave(host.trim(), uid.trim(), token.trim()) }, enabled = host.isNotBlank()) { Text("SALVAR") } },
        dismissButton = { OutlinedButton(onClick = onDismiss) { Text("CANCELAR") } },
    )
}

@Composable
private fun SettingRow(title: String, subtitle: String, modifier: Modifier = Modifier) {
    TruckCard(modifier.fillMaxWidth()) {
        Text(title, fontWeight = FontWeight.Bold, fontSize = 15.sp)
        Spacer(Modifier.height(3.dp))
        Text(subtitle, color = TruckMuted, fontSize = 10.sp)
    }
}
