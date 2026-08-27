package br.com.truckbox.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import br.com.truckbox.app.cloud.TruckBoxCloudClient
import br.com.truckbox.app.gateway.GatewayStatus
import br.com.truckbox.app.gateway.TruckBoxGatewayService
import br.com.truckbox.app.operations.OperationsStore
import br.com.truckbox.app.preferences.TruckBoxPreferences
import br.com.truckbox.app.ui.theme.TruckBoxTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

class MainActivity : ComponentActivity() {
    private val permissionLauncher = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        startGatewayServiceIfPossible()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestGatewayPermissions()
        setContent { DriverApp() }
    }

    private fun requestGatewayPermissions() {
        val permissions = buildList {
            add(Manifest.permission.ACCESS_FINE_LOCATION)
            add(Manifest.permission.ACCESS_COARSE_LOCATION)
            if (Build.VERSION.SDK_INT >= 33) add(Manifest.permission.POST_NOTIFICATIONS)
        }.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (permissions.isEmpty()) startGatewayServiceIfPossible() else permissionLauncher.launch(permissions.toTypedArray())
    }

    private fun startGatewayServiceIfPossible() {
        val fine = checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        val coarse = checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED
        if (!fine && !coarse) return
        val i = Intent(this, TruckBoxGatewayService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) startForegroundService(i) else startService(i)
    }
}

private enum class DriverPage(val title: String) { STATUS("Status"), TRIP("Viagem"), ENTRIES("Lançamentos"), SETTINGS("Config") }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DriverApp() {
    val context = androidx.compose.ui.platform.LocalContext.current
    val prefs = remember { TruckBoxPreferences(context) }
    val store = remember { OperationsStore(context) }
    val cloud = remember { TruckBoxCloudClient(prefs) }
    val scope = rememberCoroutineScope()
    val gateway by GatewayStatus.state.collectAsState()
    val operations by store.state.collectAsState()
    var page by remember { mutableStateOf(DriverPage.STATUS) }

    // Snapshot Cloud enquanto a tela estiver aberta. Os cadastros locais continuam válidos offline.
    LaunchedEffect(Unit) {
        while (isActive) {
            if (cloud.configured()) {
                withContext(Dispatchers.IO) { cloud.snapshot() }?.let(store::applyCloudSnapshot)
            }
            delay(15_000L)
        }
    }

    TruckBoxTheme(accent = prefs.accent) {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = {
                        Column {
                            Text("TruckBox Motorista", fontWeight = FontWeight.Bold)
                            Text("Gateway Android • v0.6.1", fontSize = 11.sp)
                        }
                    },
                    actions = {
                        val ok = gateway.coreOnline && (gateway.cloudOnline || gateway.pendingBytes == 0L)
                        Icon(if (ok) Icons.Default.CloudDone else Icons.Default.CloudOff, null)
                        Spacer(Modifier.width(12.dp))
                    }
                )
            },
            bottomBar = {
                NavigationBar {
                    DriverPage.entries.forEach { item ->
                        val icon = when (item) {
                            DriverPage.STATUS -> Icons.Default.Dashboard
                            DriverPage.TRIP -> Icons.Default.Route
                            DriverPage.ENTRIES -> Icons.Default.AddCircle
                            DriverPage.SETTINGS -> Icons.Default.Settings
                        }
                        NavigationBarItem(
                            selected = page == item,
                            onClick = { page = item },
                            icon = { Icon(icon, item.title) },
                            label = { Text(item.title) },
                        )
                    }
                }
            }
        ) { pad ->
            Box(Modifier.fillMaxSize().padding(pad)) {
                when (page) {
                    DriverPage.STATUS -> StatusPage(gateway)
                    DriverPage.TRIP -> TripPage(
                        active = operations.activeTrip,
                        odometerKm = gateway.odometerKm,
                        coreFuelLiters = gateway.coreFuelLiters,
                        onStart = { origin, destination, weight, rate ->
                            store.startTrip(origin, destination, weight, rate, gateway.odometerKm, gateway.coreFuelLiters ?: 0.0)
                            store.state.value.activeTrip?.let { scope.launch(Dispatchers.IO) { cloud.syncTrip(it) } }
                        },
                        onEnd = {
                            val uid = store.state.value.activeTrip?.clientUid
                            store.endActiveTrip(gateway.odometerKm, gateway.coreFuelLiters ?: 0.0)
                            uid?.let { id -> store.state.value.trips.firstOrNull { it.clientUid == id } }?.let { ended ->
                                scope.launch(Dispatchers.IO) { cloud.endTrip(ended) }
                            }
                        },
                    )
                    DriverPage.ENTRIES -> EntriesPage(
                        odometerKm = gateway.odometerKm,
                        onFueling = { liters, price, station, location, full ->
                            store.addFueling(liters, price, station, location, full, gateway.odometerKm)?.let { item ->
                                scope.launch(Dispatchers.IO) { cloud.syncFueling(item) }
                            }
                        },
                        onExpense = { category, description, amount ->
                            store.addExpense(category, description, amount)
                            store.state.value.expenses.firstOrNull()?.let { item -> scope.launch(Dispatchers.IO) { cloud.syncExpense(item) } }
                        },
                        onMaintenance = { service, nextKm, cost, notes ->
                            store.addMaintenance(service, gateway.odometerKm, nextKm, cost, notes)
                            store.state.value.maintenance.firstOrNull()?.let { item -> scope.launch(Dispatchers.IO) { cloud.syncMaintenance(item) } }
                        },
                    )
                    DriverPage.SETTINGS -> SettingsPage(prefs)
                }
            }
        }
    }
}

@Composable
private fun StatusPage(s: br.com.truckbox.app.gateway.GatewaySnapshot) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Gateway", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        StatusCard("ESP32 / Starlink", if (s.coreOnline) "ONLINE" else "OFFLINE", s.coreOnline)
        StatusCard("Cloud", if (s.cloudOnline) "ONLINE" else if (s.pendingBytes > 0) "AGUARDANDO" else "SEM ENVIO", s.cloudOnline)
        StatusCard("GPS", if (s.gpsFix) "FIX OK" else "AGUARDANDO FIX", s.gpsFix)
        ElevatedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(7.dp)) {
                Text("Fila de telemetria", fontWeight = FontWeight.Bold)
                Text("${(s.pendingBytes / 1024.0).format1()} KB pendentes")
                Text("Último ACK: ${s.lastAckSeq}")
                Text("Último lote: ${s.lastBatchCount} amostras")
                if (s.lastError != null) Text(s.lastError, color = MaterialTheme.colorScheme.error)
            }
        }
        ElevatedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(7.dp)) {
                Text("Caminhão agora", fontWeight = FontWeight.Bold)
                Text("Odômetro: ${s.odometerKm?.let { String.format(Locale.US, "%.1f km", it) } ?: "—"}")
                Text("Velocidade: ${s.speedKmh?.let { String.format(Locale.US, "%.1f km/h", it) } ?: "—"}")
                Text("RPM: ${s.rpm?.let { String.format(Locale.US, "%.0f", it) } ?: "—"}")
                Text("Peso conjunto: ${s.combinationWeightKg?.let { String.format(Locale.US, "%.1f t", it / 1000.0) } ?: "—"}")
            }
        }
    }
}

private fun Double.format1(): String = String.format(Locale.US, "%.1f", this)

@Composable
private fun StatusCard(title: String, value: String, ok: Boolean) {
    ElevatedCard(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) { Text(title, fontWeight = FontWeight.Bold); Text(value) }
            Icon(if (ok) Icons.Default.CheckCircle else Icons.Default.Warning, null,
                tint = if (ok) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error)
        }
    }
}

@Composable
private fun TripPage(
    active: br.com.truckbox.app.operations.TripRecord?,
    odometerKm: Double?,
    coreFuelLiters: Double?,
    onStart: (String, String, Double?, Double?) -> Unit,
    onEnd: () -> Unit,
) {
    var showStart by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Viagem", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        if (active == null) {
            Text("Nenhuma viagem carregada ativa.")
            Button(onClick = { showStart = true }, modifier = Modifier.fillMaxWidth()) { Text("INICIAR NOVA VIAGEM") }
        } else {
            ElevatedCard(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text("${active.origin} → ${active.destination}", fontWeight = FontWeight.Bold, fontSize = 18.sp)
                    Text("Peso: ${active.weightT?.let { "$it t" } ?: "—"}")
                    Text("Frete: ${active.freightTotal?.let { "R$ ${String.format(Locale.US, "%.2f", it)}" } ?: "—"}")
                    val distanceKm = if (active.startOdometerKm != null && odometerKm != null) {
                        (odometerKm - active.startOdometerKm).coerceAtLeast(0.0)
                    } else active.distanceKm
                    val tripFuelLiters = if (coreFuelLiters != null) {
                        (coreFuelLiters - active.startFuelCounterL).coerceAtLeast(0.0)
                    } else active.fuelLiters
                    val averageKml = if (distanceKm != null && tripFuelLiters != null && tripFuelLiters > 0.02) {
                        distanceKm / tripFuelLiters
                    } else active.averageKml
                    Text("KM inicial: ${active.startOdometerKm?.let { String.format(Locale.US, "%.1f", it) } ?: "—"}")
                    Text("KM atual: ${odometerKm?.let { String.format(Locale.US, "%.1f", it) } ?: "—"}")
                    Text("Distância da viagem: ${distanceKm?.let { String.format(Locale.US, "%.1f km", it) } ?: "—"}")
                    Text("Consumido na viagem: ${tripFuelLiters?.let { String.format(Locale.US, "%.1f L", it) } ?: "—"}")
                    Text("Média geral: ${averageKml?.let { String.format(Locale.US, "%.2f km/L", it) } ?: "—"}")
                }
            }
            Button(onClick = onEnd, modifier = Modifier.fillMaxWidth()) { Text("ENCERRAR VIAGEM") }
        }
    }
    if (showStart) StartTripDialog(onDismiss = { showStart = false }) { o, d, w, r -> onStart(o, d, w, r); showStart = false }
}

@Composable
private fun StartTripDialog(onDismiss: () -> Unit, onSave: (String, String, Double?, Double?) -> Unit) {
    var origin by remember { mutableStateOf("") }; var destination by remember { mutableStateOf("") }
    var weight by remember { mutableStateOf("") }; var rate by remember { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Nova viagem") },
        text = { Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(origin, { origin = it }, label = { Text("Origem") })
            OutlinedTextField(destination, { destination = it }, label = { Text("Destino") })
            OutlinedTextField(weight, { weight = it }, label = { Text("Peso (t)") })
            OutlinedTextField(rate, { rate = it }, label = { Text("Valor por tonelada") })
        } },
        confirmButton = { Button(onClick = { onSave(origin, destination, weight.num(), rate.num()) }, enabled = origin.isNotBlank() && destination.isNotBlank()) { Text("SALVAR") } },
        dismissButton = { OutlinedButton(onClick = onDismiss) { Text("CANCELAR") } },
    )
}

@Composable
private fun EntriesPage(
    odometerKm: Double?,
    onFueling: (Double, Double, String, String, Boolean) -> Unit,
    onExpense: (String, String, Double) -> Unit,
    onMaintenance: (String, Double?, Double?, String) -> Unit,
) {
    var dialog by remember { mutableStateOf<String?>(null) }
    Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Lançamentos", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text("Odômetro atual: ${odometerKm?.let { String.format(Locale.US, "%.1f km", it) } ?: "—"}")
        Button(onClick = { dialog = "fuel" }, modifier = Modifier.fillMaxWidth()) { Icon(Icons.Default.LocalGasStation, null); Spacer(Modifier.width(8.dp)); Text("ABASTECIMENTO") }
        Button(onClick = { dialog = "expense" }, modifier = Modifier.fillMaxWidth()) { Icon(Icons.Default.ReceiptLong, null); Spacer(Modifier.width(8.dp)); Text("DESPESA") }
        OutlinedButton(onClick = { dialog = "maint" }, modifier = Modifier.fillMaxWidth()) { Icon(Icons.Default.Build, null); Spacer(Modifier.width(8.dp)); Text("MANUTENÇÃO") }
    }
    when (dialog) {
        "fuel" -> FuelDialog({ dialog = null }) { l,p,s,loc,full -> onFueling(l,p,s,loc,full); dialog=null }
        "expense" -> ExpenseDialog({ dialog = null }) { c,d,a -> onExpense(c,d,a); dialog=null }
        "maint" -> MaintenanceDialog({ dialog = null }) { s,n,c,no -> onMaintenance(s,n,c,no); dialog=null }
    }
}

@Composable private fun FuelDialog(onDismiss:()->Unit,onSave:(Double,Double,String,String,Boolean)->Unit){
    var l by remember{mutableStateOf("")};var p by remember{mutableStateOf("")};var s by remember{mutableStateOf("")};var loc by remember{mutableStateOf("")};var full by remember{mutableStateOf(false)}
    AlertDialog(onDismissRequest=onDismiss,title={Text("Abastecimento")},text={Column(verticalArrangement=Arrangement.spacedBy(8.dp)){
        OutlinedTextField(l,{l=it},label={Text("Litros")});OutlinedTextField(p,{p=it},label={Text("Preço/L")});OutlinedTextField(s,{s=it},label={Text("Posto")});OutlinedTextField(loc,{loc=it},label={Text("Local")});Row(verticalAlignment=Alignment.CenterVertically){Checkbox(full,{full=it});Text("Tanque cheio")}
    }},confirmButton={Button(onClick={onSave(l.num()?:0.0,p.num()?:0.0,s,loc,full)},enabled=(l.num()?:0.0)>0){Text("SALVAR")}},dismissButton={OutlinedButton(onClick=onDismiss){Text("CANCELAR")}})
}

@Composable private fun ExpenseDialog(onDismiss:()->Unit,onSave:(String,String,Double)->Unit){
    var c by remember{mutableStateOf("Outros")};var d by remember{mutableStateOf("")};var a by remember{mutableStateOf("")}
    AlertDialog(onDismissRequest=onDismiss,title={Text("Despesa")},text={Column(verticalArrangement=Arrangement.spacedBy(8.dp)){
        OutlinedTextField(c,{c=it},label={Text("Categoria")});OutlinedTextField(d,{d=it},label={Text("Descrição")});OutlinedTextField(a,{a=it},label={Text("Valor")})
    }},confirmButton={Button(onClick={onSave(c,d,a.num()?:0.0)},enabled=(a.num()?:-1.0)>=0){Text("SALVAR")}},dismissButton={OutlinedButton(onClick=onDismiss){Text("CANCELAR")}})
}

@Composable private fun MaintenanceDialog(onDismiss:()->Unit,onSave:(String,Double?,Double?,String)->Unit){
    var s by remember{mutableStateOf("")};var n by remember{mutableStateOf("")};var c by remember{mutableStateOf("")};var notes by remember{mutableStateOf("")}
    AlertDialog(onDismissRequest=onDismiss,title={Text("Manutenção")},text={Column(verticalArrangement=Arrangement.spacedBy(8.dp)){
        OutlinedTextField(s,{s=it},label={Text("Serviço")});OutlinedTextField(n,{n=it},label={Text("Próxima troca (km)")});OutlinedTextField(c,{c=it},label={Text("Custo")});OutlinedTextField(notes,{notes=it},label={Text("Observações")})
    }},confirmButton={Button(onClick={onSave(s,n.num(),c.num(),notes)},enabled=s.isNotBlank()){Text("SALVAR")}},dismissButton={OutlinedButton(onClick=onDismiss){Text("CANCELAR")}})
}

@Composable
private fun SettingsPage(prefs: TruckBoxPreferences) {
    var host by remember { mutableStateOf(prefs.coreHost) }
    var uid by remember { mutableStateOf(prefs.cloudDeviceUid) }
    var token by remember { mutableStateOf("") }
    var gpsUid by remember { mutableStateOf(prefs.gpsDeviceUid) }
    var gpsToken by remember { mutableStateOf("") }
    var enabled by remember { mutableStateOf(prefs.gatewayEnabled) }
    var saved by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text("Configuração", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        OutlinedTextField(host, { host = it }, label = { Text("Core na Starlink") }, supportingText = { Text("truckbox.local ou IP LAN do ESP32") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(uid, { uid = it }, label = { Text("Device UID Core") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(token, { token = it }, label = { Text(if (prefs.cloudDeviceToken.isBlank()) "Token Cloud Core" else "Novo token Core (deixe vazio para manter)") }, visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())
        HorizontalDivider()
        Text("GPS", fontWeight = FontWeight.Bold)
        OutlinedTextField(gpsUid, { gpsUid = it }, label = { Text("Device UID GPS") }, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(gpsToken, { gpsToken = it }, label = { Text(if (prefs.gpsDeviceToken.isBlank()) "Token Cloud GPS" else "Novo token GPS (deixe vazio para manter)") }, visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())
        Row(verticalAlignment = Alignment.CenterVertically) { Switch(enabled, { enabled = it }); Spacer(Modifier.width(8.dp)); Text("Gateway em segundo plano") }
        Button(onClick = {
            prefs.coreHost = host
            prefs.cloudDeviceUid = uid
            if (token.isNotBlank()) prefs.cloudDeviceToken = token
            prefs.gpsDeviceUid = gpsUid
            if (gpsToken.isNotBlank()) prefs.gpsDeviceToken = gpsToken
            prefs.gatewayEnabled = enabled
            token = ""; gpsToken = ""; saved = true
        }, modifier = Modifier.fillMaxWidth()) { Text("SALVAR") }
        if (saved) Text("Configuração salva. O serviço lê as novas opções automaticamente.", color = MaterialTheme.colorScheme.primary)
        Text("A multimídia não participa do Gateway. Ela será tratada como aplicativo separado.", fontSize = 12.sp)
    }
}

private fun String.num(): Double? = trim().replace(',', '.').toDoubleOrNull()
