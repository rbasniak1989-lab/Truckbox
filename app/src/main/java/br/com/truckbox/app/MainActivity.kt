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
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.repeatOnLifecycle
import androidx.lifecycle.lifecycleScope
import br.com.truckbox.app.cloud.DriverOperationsSync
import br.com.truckbox.app.gps.DriverGpsOutbox
import br.com.truckbox.app.cloud.RoutePlannerClient
import br.com.truckbox.app.cloud.TripRoutesClient
import br.com.truckbox.app.gateway.GatewayStatus
import br.com.truckbox.app.gateway.TruckBoxGatewayService
import br.com.truckbox.app.operations.OperationsStore
import br.com.truckbox.app.operations.OperationsSnapshot
import br.com.truckbox.app.operations.DriverAction
import br.com.truckbox.app.data.OperationMode
import br.com.truckbox.app.operations.RoutePlanSnapshot
import br.com.truckbox.app.preferences.TruckBoxPreferences
import br.com.truckbox.app.ui.TripRoutePanel
import br.com.truckbox.app.ui.theme.TruckBoxTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

class MainActivity : ComponentActivity() {
    private val sharedRouteText = mutableStateOf<String?>(null)
    private val permissionLauncher = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        startGatewayServiceIfPossible()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        sharedRouteText.value = extractSharedRoute(intent)
        GatewayStatus.update { it.copy(gpsEnabled = TruckBoxPreferences(this).gpsTrackingEnabled) }
        lifecycleScope.launch(Dispatchers.IO) {
            runCatching { DriverGpsOutbox(this@MainActivity).use { it.countAll() } }
                .onSuccess { count -> GatewayStatus.update { it.copy(pendingGps = count) } }
                .onFailure { GatewayStatus.update { it.copy(lastError = "Não foi possível consultar a fila GPS do celular") } }
        }
        setContent {
            DriverApp(
                sharedRouteText = sharedRouteText.value,
                onSharedRouteConsumed = { sharedRouteText.value = null },
                onStartGps = ::requestGatewayPermissions,
                onStopGps = { stopService(Intent(this, TruckBoxGatewayService::class.java)) },
            )
        }
    }

    override fun onResume() {
        super.onResume()
        startGatewayServiceIfPossible()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        extractSharedRoute(intent)?.let { sharedRouteText.value = it }
    }

    private fun extractSharedRoute(intent: Intent?): String? {
        if (intent?.action != Intent.ACTION_SEND || intent.type != "text/plain") return null
        val raw = intent.getStringExtra(Intent.EXTRA_TEXT)?.trim().orEmpty()
        if (raw.isBlank()) return null
        return Regex("""https?://\S+""").find(raw)?.value?.trimEnd('.', ',', ')', ']') ?: raw
    }

    private fun requestGatewayPermissions() {
        val permissions = buildList {
            if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
                add(Manifest.permission.ACCESS_FINE_LOCATION)
                add(Manifest.permission.ACCESS_COARSE_LOCATION)
            }
            if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                add(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
        if (permissions.isEmpty()) startGatewayServiceIfPossible() else permissionLauncher.launch(permissions.toTypedArray())
    }

    private fun startGatewayServiceIfPossible() {
        val prefs = TruckBoxPreferences(this)
        if (!prefs.gpsTrackingEnabled || !runCatching { prefs.gpsCredentials().configured }.getOrDefault(false)) return
        val fine = checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        val coarse = checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED
        if (!fine && !coarse) {
            GatewayStatus.update { it.copy(running = false, lastError = "Permita a localização para iniciar o GPS") }
            return
        }
        val i = Intent(this, TruckBoxGatewayService::class.java)
        try {
            startForegroundService(i)
        } catch (_: SecurityException) {
            GatewayStatus.update { it.copy(running = false, lastError = "Permita a localização para iniciar o GPS") }
        } catch (_: IllegalStateException) {
            GatewayStatus.update { it.copy(running = false, lastError = "Abra o Motorista e toque em Iniciar GPS") }
        }
    }
}

private enum class DriverPage(val title: String) { STATUS("Status"), TRIP("Viagem"), ENTRIES("Lançamentos"), SETTINGS("Config") }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DriverApp(
    sharedRouteText: String?,
    onSharedRouteConsumed: () -> Unit,
    onStartGps: () -> Unit,
    onStopGps: () -> Unit,
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val prefs = remember { TruckBoxPreferences(context) }
    val store = remember { OperationsStore.get(context) }
    val routePlanner = remember { RoutePlannerClient(prefs) }
    val tripRoutes = remember { TripRoutesClient(prefs) }
    val scope = rememberCoroutineScope()
    val gateway by GatewayStatus.state.collectAsState()
    val operations by store.state.collectAsState()
    var page by remember { mutableStateOf(DriverPage.STATUS) }
    var routePlan by remember { mutableStateOf<RoutePlanSnapshot?>(null) }
    var routePlanning by remember { mutableStateOf(false) }
    var routePlanError by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(sharedRouteText) {
        if (!sharedRouteText.isNullOrBlank()) page = DriverPage.TRIP
    }

    val lifecycle = LocalLifecycleOwner.current.lifecycle
    LaunchedEffect(lifecycle) {
        lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            while (isActive) {
                withContext(Dispatchers.IO) { DriverOperationsSync.sync(prefs, store) }
                delay(5_000L)
            }
        }
    }

    TruckBoxTheme(accent = prefs.accent) {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = {
                        Column {
                            Text("TruckBox Motorista", fontWeight = FontWeight.Bold)
                            Text("Cloud e GPS do celular • v0.6.8", fontSize = 11.sp)
                        }
                    },
                    actions = {
                        val ok = gateway.cloudOnline
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
                        operations = operations,
                        tripRoutes = tripRoutes,
                        routePlan = routePlan,
                        routePlanning = routePlanning,
                        routePlanError = routePlanError,
                        sharedRouteText = sharedRouteText,
                        onSharedRouteConsumed = onSharedRouteConsumed,
                        onCalculateRoute = { link, origin, destination, weight, rate ->
                            routePlanning = true
                            routePlanError = null
                            scope.launch {
                                val response = withContext(Dispatchers.IO) {
                                    routePlanner.calculate(link, origin, destination, weight, rate)
                                }
                                routePlanning = false
                                routePlan = response.plan
                                routePlanError = response.error
                            }
                        },
                        onStartPlan = { plan ->
                            store.startTrip(
                                plan.origin, plan.destination, plan.cargoWeightT, plan.ratePerT,
                                null, null, plan
                            )
                            routePlan = null
                            routePlanError = null
                        },
                        onDiscardPlan = { routePlan = null; routePlanError = null },
                        onStart = { origin, destination, weight, rate ->
                            store.startTrip(origin, destination, weight, rate, null, null)
                        },
                        onEnd = {
                            store.endActiveTrip(null, null)
                        },
                    )
                    DriverPage.ENTRIES -> EntriesPage(
                        onFueling = { liters, price, station, location, full ->
                            store.addFueling(liters, price, station, location, full, null)
                        },
                        onExpense = { category, description, amount ->
                            store.addExpense(category, description, amount)
                        },
                        onMaintenance = { service, nextKm, cost, notes ->
                            store.addMaintenance(service, null, nextKm, cost, notes)
                        },
                    )
                    DriverPage.SETTINGS -> SettingsPage(prefs, store, onStartGps, onStopGps)
                }
            }
        }
    }
}

@Composable
internal fun StatusPage(s: br.com.truckbox.app.gateway.GatewaySnapshot) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Cloud e GPS", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        StatusCard("Viagens e lançamentos", if (s.operationsCloudOnline) "Resposta recebida da Cloud" else "Sem confirmação da Cloud", s.operationsCloudOnline)
        StatusCard("GPS do celular", when {
            !s.gpsEnabled -> "Pausado"
            s.gpsFix -> "Posição recente recebida"
            else -> "Aguardando posição"
        }, s.gpsFix)
        ElevatedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(7.dp)) {
                Text("Envio de posições", fontWeight = FontWeight.Bold)
                Text("Serviço GPS: ${if (s.running) "em execução" else "parado"}")
                Text("Última captura: ${s.lastGpsCaptureMs.clockText()}")
                Text("Precisão informada: ${s.gpsAccuracyM?.let { "${it.format1()} m" } ?: "Sem leitura"}")
                Text("Última posição confirmada na Cloud: ${s.lastGpsSentCaptureMs.clockText()}")
                Text("Último envio confirmado: ${s.lastGpsUploadMs.clockText()}")
                Text("${s.pendingGps} posições aguardando envio")
                Text("${s.pendingOperations} operações aguardando sincronização")
                s.lastError?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                s.operationsError?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            }
        }
        Text("O GPS vem deste celular. Mantenha o serviço ativo e a notificação visível durante a viagem. Sem internet, as posições ficam guardadas para reenvio.", fontSize = 12.sp)
    }
}

private fun Long?.clockText(): String = this?.let {
    java.time.Instant.ofEpochMilli(it).atZone(java.time.ZoneId.systemDefault())
        .format(java.time.format.DateTimeFormatter.ofPattern("dd/MM HH:mm:ss"))
} ?: "Sem confirmação"

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
    operations: OperationsSnapshot,
    tripRoutes: TripRoutesClient,
    routePlan: RoutePlanSnapshot?,
    routePlanning: Boolean,
    routePlanError: String?,
    sharedRouteText: String?,
    onSharedRouteConsumed: () -> Unit,
    onCalculateRoute: (String, String, String, Double, Double?) -> Unit,
    onStartPlan: (RoutePlanSnapshot) -> Unit,
    onDiscardPlan: () -> Unit,
    onStart: (String, String, Double?, Double?) -> Unit,
    onEnd: () -> Unit,
) {
    val active = operations.activeTrip
    var showStart by remember { mutableStateOf(false) }
    var showCalculate by remember { mutableStateOf(false) }
    var sharedLink by remember { mutableStateOf("") }

    LaunchedEffect(sharedRouteText) {
        if (!sharedRouteText.isNullOrBlank()) {
            sharedLink = sharedRouteText
            showCalculate = true
            onSharedRouteConsumed()
        }
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Viagem Atual", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        if (operations.pendingCommands.any { it.awaitingTripReview }) {
            Text("Há registros de viagem offline preservados para revisão.", color = MaterialTheme.colorScheme.error)
        }
        if (active == null) {
            if (operations.mode == OperationMode.EMPTY_MODE) {
                val distance = operations.emptyDistanceKm
                val diesel = operations.emptyFuelLiters
                val average = if (distance != null && diesel != null && diesel > 0.02) distance / diesel else null
                ElevatedCard(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(9.dp)) {
                        Text("MODO VAZIO — DESDE A DESCARGA", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Column(Modifier.weight(1f)) {
                                Text("Distância", fontSize = 11.sp)
                                Text(distance?.let { String.format(Locale.US, "%.1f km", it) } ?: "—", fontWeight = FontWeight.Bold)
                            }
                            Column(Modifier.weight(1f)) {
                                Text("Consumido", fontSize = 11.sp)
                                Text(diesel?.let { String.format(Locale.US, "%.1f L", it) } ?: "—", fontWeight = FontWeight.Bold)
                            }
                            Column(Modifier.weight(1f)) {
                                Text("Média", fontSize = 11.sp)
                                Text(average?.let { String.format(Locale.US, "%.2f km/L", it) } ?: "—", fontWeight = FontWeight.Bold)
                            }
                        }
                        Text("Acumulado deste trecho vazio até a próxima carga. Valores sincronizados com a Cloud.", fontSize = 11.sp)
                    }
                }
            } else {
                Text("Nenhuma viagem carregada ativa.")
            }
            Button(onClick = { sharedLink = ""; showCalculate = true }, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Default.Route, null); Spacer(Modifier.width(8.dp)); Text("CALCULAR ROTA")
            }
            OutlinedButton(onClick = { showStart = true }, modifier = Modifier.fillMaxWidth()) { Text("INICIAR NOVA VIAGEM") }

            if (routePlanning) {
                ElevatedCard(Modifier.fillMaxWidth()) {
                    Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(Modifier.size(24.dp)); Spacer(Modifier.width(12.dp))
                        Text("Calculando rota, relevo e previsão de diesel…", fontWeight = FontWeight.Bold)
                    }
                }
            }
            if (!routePlanError.isNullOrBlank()) {
                Text(routePlanError, color = MaterialTheme.colorScheme.error, fontWeight = FontWeight.Bold)
            }
            routePlan?.let { plan ->
                ElevatedCard(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Text("SIMULAÇÃO DE ROTA", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                        Text("${plan.origin} → ${plan.destination}", fontWeight = FontWeight.Bold, fontSize = 18.sp)
                        Text("Carga: ${String.format(Locale.US, "%.1f t", plan.cargoWeightT)} • Peso operacional: ${String.format(Locale.US, "%.1f t", plan.grossWeightT)}")
                        Text("Distância: ${plan.routeDistanceKm?.let { String.format(Locale.US, "%.1f km", it) } ?: "—"}")
                        Text("Subida: ${plan.ascentM?.let { String.format(Locale.US, "%.0f m", it) } ?: "—"} • Descida: ${plan.descentM?.let { String.format(Locale.US, "%.0f m", it) } ?: "—"}")
                        Text("Consumo previsto: ${plan.expectedKml?.let { String.format(Locale.US, "%.2f km/L", it) } ?: "—"}", fontWeight = FontWeight.Bold)
                        Text("Diesel previsto: ${plan.predictedFuelLiters?.let { String.format(Locale.US, "%.1f L", it) } ?: "—"}", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Black)
                        if (plan.predictedFuelLowL != null && plan.predictedFuelHighL != null) {
                            Text("Faixa: ${String.format(Locale.US, "%.0f–%.0f L", plan.predictedFuelLowL, plan.predictedFuelHighL)} • Confiança: ${plan.confidence.uppercase()}")
                        }
                        Text("Memória da estrada: ${plan.routeMemoryCoveragePct?.let { String.format(Locale.US, "%.0f%%", it) } ?: "0%"} • Modelo de relevo: ${plan.geographyModelCoveragePct?.let { String.format(Locale.US, "%.0f%%", it) } ?: "0%"}", fontSize = 12.sp)
                        Button(onClick = { onStartPlan(plan) }, modifier = Modifier.fillMaxWidth()) { Text("INICIAR ESTA VIAGEM") }
                        OutlinedButton(onClick = onDiscardPlan, modifier = Modifier.fillMaxWidth()) { Text("DESCARTAR SIMULAÇÃO") }
                    }
                }
            }
        } else {
            val ending = operations.pendingCommands.any { it.action == DriverAction.END_TRIP && it.entityUid == active.clientUid }
            ElevatedCard(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text("${active.origin} → ${active.destination}", fontWeight = FontWeight.Bold, fontSize = 18.sp)
                    val br = Locale.forLanguageTag("pt-BR")
                    val money = java.text.NumberFormat.getCurrencyInstance(br)
                    val start = java.text.DateFormat.getDateTimeInstance(java.text.DateFormat.SHORT, java.text.DateFormat.SHORT, br)
                    Text("Início · ${start.format(java.util.Date(active.startedAtMs))}")
                    Text("Carga · ${active.weightT?.let { "${String.format(br, "%.1f", it)} t" } ?: "—"}")
                    Text("Frete bruto · ${active.freightTotal?.let(money::format) ?: "—"}")
                    active.ratePerT?.let { Text("Frete por tonelada · ${money.format(it)}") }
                    active.startOdometerKm?.let { Text("Odômetro inicial · ${String.format(br, "%.1f", it)} km") }
                    active.notes?.let { Text(it) }
                    val distanceKm = active.distanceKm
                    val tripFuelLiters = active.fuelLiters
                    val averageKml = active.averageKml
                    Text("Métricas da viagem recebidas da Cloud", fontSize = 12.sp)
                    Text("Distância da viagem: ${distanceKm?.let { String.format(Locale.US, "%.1f km", it) } ?: "—"}")
                    Text("Consumido na viagem: ${tripFuelLiters?.let { String.format(Locale.US, "%.1f L", it) } ?: "—"}")
                    Text("Média geral: ${averageKml?.let { String.format(Locale.US, "%.2f km/L", it) } ?: "—"}")
                    if (active.emptyBeforeDistanceKm != null || active.emptyBeforeFuelLiters != null) {
                        val emptyDistance = active.emptyBeforeDistanceKm?.let { String.format(br, "%.1f km", it) }
                        val emptyFuel = active.emptyBeforeFuelLiters?.let { String.format(br, "%.1f L", it) }
                        Text("Trecho vazio anterior · ${listOfNotNull(emptyDistance, emptyFuel).joinToString(" · ")}")
                    }
                    if (active.plannedFuelLiters != null) {
                        HorizontalDivider()
                        Text("Previsto no início: ${String.format(Locale.US, "%.1f L", active.plannedFuelLiters)}", fontWeight = FontWeight.Bold)
                        active.plannedDistanceKm?.let { Text("Rota planejada: ${String.format(Locale.US, "%.1f km", it)}") }
                    }
                }
            }
            key(active.cloudId ?: active.clientUid) { TripRoutePanel(active, tripRoutes, ending) }
            Button(onClick = { sharedLink = ""; showCalculate = true }, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Default.Route, null); Spacer(Modifier.width(8.dp)); Text("CALCULAR PRÓXIMA ROTA")
            }
            if (routePlanning) {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.size(24.dp)); Spacer(Modifier.width(12.dp))
                    Text("Calculando rota, relevo e previsão de diesel…", fontWeight = FontWeight.Bold)
                }
            }
            if (!routePlanError.isNullOrBlank()) {
                Text(routePlanError, color = MaterialTheme.colorScheme.error, fontWeight = FontWeight.Bold)
            }
            routePlan?.let { plan ->
                ElevatedCard(Modifier.fillMaxWidth()) {
                    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        Text("SIMULAÇÃO DA PRÓXIMA ROTA", color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.Bold)
                        Text("${plan.origin} → ${plan.destination}", fontWeight = FontWeight.Bold)
                        Text("Diesel previsto: ${plan.predictedFuelLiters?.let { String.format(Locale.US, "%.1f L", it) } ?: "—"}", fontWeight = FontWeight.Black)
                        Text("Encerre a viagem atual para habilitar “Iniciar esta viagem”.", fontSize = 12.sp)
                        OutlinedButton(onClick = onDiscardPlan, modifier = Modifier.fillMaxWidth()) { Text("DESCARTAR SIMULAÇÃO") }
                    }
                }
            }
            Button(onClick = onEnd, modifier = Modifier.fillMaxWidth(), enabled = !ending) { Text("ENCERRAR VIAGEM") }
        }
    }
    if (showStart) StartTripDialog(onDismiss = { showStart = false }) { o, d, w, r -> onStart(o, d, w, r); showStart = false }
    if (showCalculate) CalculateRouteDialog(
        initialLink = sharedLink,
        onDismiss = { showCalculate = false },
        onCalculate = { link, o, d, w, r ->
            onCalculateRoute(link, o, d, w, r)
            showCalculate = false
        },
    )
}

@Composable
private fun CalculateRouteDialog(
    initialLink: String,
    onDismiss: () -> Unit,
    onCalculate: (String, String, String, Double, Double?) -> Unit,
) {
    var link by remember(initialLink) { mutableStateOf(initialLink) }
    var origin by remember { mutableStateOf("") }
    var destination by remember { mutableStateOf("") }
    var weight by remember { mutableStateOf("") }
    var rate by remember { mutableStateOf("") }
    val weightValue = weight.num()

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Calcular rota") },
        text = {
            Column(
                Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedTextField(
                    link, { link = it },
                    label = { Text("Rota do Google Maps") },
                    supportingText = { Text("Cole o link ou use Compartilhar → TruckBox no Google Maps.") },
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(origin, { origin = it }, label = { Text("Origem") }, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(destination, { destination = it }, label = { Text("Destino") }, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(weight, { weight = it }, label = { Text("Carga (t)") }, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(rate, { rate = it }, label = { Text("Valor por tonelada (opcional)") }, modifier = Modifier.fillMaxWidth())
                Text("A simulação não cria uma viagem. Ela só vira viagem real quando você tocar em Iniciar esta viagem.", fontSize = 12.sp)
            }
        },
        confirmButton = {
            Button(
                onClick = { onCalculate(link, origin, destination, weightValue ?: 0.0, rate.num()) },
                enabled = link.isNotBlank() && origin.isNotBlank() && destination.isNotBlank() && weightValue != null && weightValue >= 0,
            ) { Text("CALCULAR") }
        },
        dismissButton = { OutlinedButton(onClick = onDismiss) { Text("CANCELAR") } },
    )
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
    onFueling: (Double, Double, String, String, Boolean) -> Unit,
    onExpense: (String, String, Double) -> Unit,
    onMaintenance: (String, Double?, Double?, String) -> Unit,
) {
    var dialog by remember { mutableStateOf<String?>(null) }
    Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Lançamentos", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text("Os lançamentos ficam salvos no celular até a confirmação da Cloud.")
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
internal fun SettingsPage(prefs: TruckBoxPreferences, store: OperationsStore, onStartGps: () -> Unit, onStopGps: () -> Unit) {
    var code by remember { mutableStateOf("") }
    var enabled by remember { mutableStateOf(prefs.gpsTrackingEnabled) }
    var configured by remember { mutableStateOf(runCatching { prefs.gpsCredentials().configured }.getOrDefault(false)) }
    var message by remember { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()
    var saving by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Configuração", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text("Acesso à Cloud", fontWeight = FontWeight.Bold)
        Text(if (configured) "Acesso salvo neste celular. A validação acontece na Cloud." else "Cole o código privado do Motorista para configurar viagens e GPS.")
        OutlinedTextField(code, { code = it }, label = { Text("Código privado do Motorista") },
            visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth(),
            supportingText = { Text("Use o código privado já emitido para este Motorista.") })
        Button(onClick = {
            saving = true
            scope.launch {
                val result = withContext(Dispatchers.IO) {
                    runCatching {
                        prefs.importCloudCode(code.trim())
                        store.bindUnassignedCommands(prefs.cloudCredentials().scope)
                    }
                }
                saving = false
                if (result.isSuccess) {
                    code = ""; configured = true
                    message = "Acesso salvo. Confira a confirmação na tela Status."
                    if (enabled) onStartGps()
                } else message = "Não foi possível salvar o código. Confira o arquivo do Motorista e tente novamente."
            }
        }, enabled = code.isNotBlank() && !saving, modifier = Modifier.fillMaxWidth()) { Text(if (saving) "SALVANDO…" else "SALVAR ACESSO") }
        message?.let { Text(it) }
        HorizontalDivider()
        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(enabled, {
                enabled = it; prefs.gpsTrackingEnabled = it
                GatewayStatus.update { state -> state.copy(gpsEnabled = it, gpsFix = false) }
                if (it) onStartGps() else onStopGps()
            })
            Spacer(Modifier.width(8.dp)); Text("Compartilhar GPS com a Cloud")
        }
        Text("Durante a viagem, a localização é enviada mesmo com a tela apagada enquanto o serviço e sua notificação estiverem ativos. Pausar interrompe a coleta e o reenvio; a fila anterior permanece no celular.", fontSize = 12.sp)
        OutlinedButton(onClick = onStartGps, enabled = configured && enabled, modifier = Modifier.fillMaxWidth()) { Text("INICIAR GPS / REVISAR PERMISSÕES") }
        Text("Permita a localização precisa. O celular usa sua conexão de internet disponível para enviar as posições. As viagens são consultadas diretamente na Cloud.", fontSize = 12.sp)
    }
}

private fun String.num(): Double? = trim().replace(',', '.').toDoubleOrNull()?.takeIf { it.isFinite() }
