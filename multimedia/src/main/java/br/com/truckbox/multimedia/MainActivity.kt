package br.com.truckbox.multimedia

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.weight
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Build
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.CloudOff
import androidx.compose.material.icons.filled.Dashboard
import androidx.compose.material.icons.filled.LocalGasStation
import androidx.compose.material.icons.filled.Map
import androidx.compose.material.icons.filled.Route
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContent { TruckBoxMultimediaApp() }
    }
}

private enum class Page(val title: String) {
    HOME("Início"),
    FUEL("Consumo"),
    TRIP("Viagem"),
    SENSORS("Sensores"),
    DIAGNOSTICS("Diagnóstico"),
    SETTINGS("Config")
}

private data class LiveState(
    val online: Boolean = false,
    val source: String = "OFFLINE",
    val updatedAtMs: Long = 0L,
    val speedKmh: Double? = null,
    val rpm: Double? = null,
    val acceleratorPct: Double? = null,
    val torquePct: Double? = null,
    val torqueNm: Double? = null,
    val powerKw: Double? = null,
    val nominalFrictionTorquePct: Double? = null,
    val retarderTorquePct: Double? = null,
    val currentGear: Int? = null,
    val selectedGear: Int? = null,
    val coolantTempC: Double? = null,
    val oilTempC: Double? = null,
    val fuelTempC: Double? = null,
    val ambientTempC: Double? = null,
    val intakeTempC: Double? = null,
    val oilLevelPct: Double? = null,
    val coolantLevelPct: Double? = null,
    val oilPressureBar: Double? = null,
    val fuelPressureBar: Double? = null,
    val coolantPressureBar: Double? = null,
    val crankcasePressureKpa: Double? = null,
    val airInletPressureKpa: Double? = null,
    val airCleanerDiffPressureKpa: Double? = null,
    val boostBar: Double? = null,
    val gearboxTempC: Double? = null,
    val clutchSlipPct: Double? = null,
    val transInputRpm: Double? = null,
    val transOutputRpm: Double? = null,
    val brakePedalPct: Double? = null,
    val tachoSpeedKmh: Double? = null,
    val tachoOutputRpm: Double? = null,
    val absActive: Boolean? = null,
    val absAmberWarning: Boolean? = null,
    val combinationWeightKg: Double? = null,
    val driveBogieKg: Double? = null,
    val axle1fWeightKg: Double? = null,
    val axle2fWeightKg: Double? = null,
    val odometerKm: Double? = null,
    val coreFuelLiters: Double? = null,
    val fuelRateLph: Double? = null,
    val tripDistanceKm: Double? = null,
    val tripFuelLiters: Double? = null,
    val tripAverageKml: Double? = null,
    val last100KmAverageKml: Double? = null,
    val ecoScore: Double? = null,
    val iRollActive: Boolean? = null,
    val cruiseActive: Boolean? = null,
    val vebStage: Int? = null,
    val origin: String? = null,
    val destination: String? = null,
    val tripWeightT: Double? = null,
    val latitude: Double? = null,
    val longitude: Double? = null,
    val error: String? = null,
)

@OptIn(ExperimentalFoundationApi::class, ExperimentalMaterial3Api::class)
@Composable
private fun TruckBoxMultimediaApp() {
    val context = LocalContext.current
    val prefs = remember { context.getSharedPreferences("truckbox_multimedia", Context.MODE_PRIVATE) }
    var coreHost by remember { mutableStateOf(prefs.getString("core_host", "192.168.4.1") ?: "192.168.4.1") }
    var live by remember { mutableStateOf(LiveState()) }
    val pages = Page.entries
    val pagerState = rememberPagerState(pageCount = { pages.size })
    val scope = rememberCoroutineScope()

    LaunchedEffect(coreHost) {
        val repository = LiveRepository(coreHost)
        while (isActive) {
            val fresh = withContext(Dispatchers.IO) { repository.fetch() }
            live = if (fresh != null) fresh else live.copy(online = false, source = "OFFLINE", error = "Sem resposta do Core/Cloud")
            delay(if (fresh?.source == "CORE") 1_000L else 4_000L)
        }
    }

    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Color(0xFF44D27A),
            secondary = Color(0xFF62A9FF),
            surface = Color(0xFF151A20),
            background = Color(0xFF0A0D10),
            error = Color(0xFFFF6B6B),
        )
    ) {
        Scaffold(
            containerColor = MaterialTheme.colorScheme.background,
            topBar = { TopStatusBar(live) },
            bottomBar = {
                NavigationBar(containerColor = Color(0xFF11161C)) {
                    pages.forEachIndexed { index, page ->
                        NavigationBarItem(
                            selected = pagerState.currentPage == index,
                            onClick = { scope.launch { pagerState.animateScrollToPage(index) } },
                            icon = {
                                Icon(
                                    when (page) {
                                        Page.HOME -> Icons.Default.Dashboard
                                        Page.FUEL -> Icons.Default.LocalGasStation
                                        Page.TRIP -> Icons.Default.Route
                                        Page.SENSORS -> Icons.Default.Sensors
                                        Page.DIAGNOSTICS -> Icons.Default.Build
                                        Page.SETTINGS -> Icons.Default.Settings
                                    },
                                    contentDescription = page.title,
                                )
                            },
                            label = { Text(page.title, fontSize = 11.sp) }
                        )
                    }
                }
            }
        ) { padding ->
            HorizontalPager(
                state = pagerState,
                modifier = Modifier.fillMaxSize().padding(padding)
            ) { index ->
                when (pages[index]) {
                    Page.HOME -> HomePage(live)
                    Page.FUEL -> FuelPage(live)
                    Page.TRIP -> TripPage(live)
                    Page.SENSORS -> SensorsPage(live)
                    Page.DIAGNOSTICS -> DiagnosticsPage(live)
                    Page.SETTINGS -> SettingsPage(
                        coreHost = coreHost,
                        onSave = {
                            val normalized = it.trim().ifBlank { "192.168.4.1" }
                            prefs.edit().putString("core_host", normalized).apply()
                            coreHost = normalized
                        },
                        live = live,
                    )
                }
            }
        }
    }
}

@Composable
private fun TopStatusBar(s: LiveState) {
    Surface(color = Color(0xFF11161C)) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("TRUCKBOX", fontWeight = FontWeight.Black, fontSize = 22.sp)
            Spacer(Modifier.width(14.dp))
            Text("Multimídia • v0.2", color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 12.sp)
            Spacer(Modifier.weight(1f))
            val status = when (s.source) {
                "CORE" -> "CORE AO VIVO"
                "CLOUD" -> "CLOUD"
                else -> "SEM CONEXÃO"
            }
            Icon(if (s.online) Icons.Default.Cloud else Icons.Default.CloudOff, status, tint = if (s.online) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error)
            Spacer(Modifier.width(8.dp))
            Text(status, fontWeight = FontWeight.Bold, fontSize = 12.sp)
        }
    }
}

@Composable
private fun HomePage(s: LiveState) {
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            HeroCard("VELOCIDADE", s.speedKmh?.fmt0() ?: "—", "km/h", Modifier.weight(1.25f), 54)
            HeroCard("RPM", s.rpm?.fmt0() ?: "—", "rpm", Modifier.weight(1f), 42)
            GearCard(s, Modifier.weight(1f))
            HeroCard("MÉDIA", s.effectiveAverage()?.fmt2() ?: "—", "km/L", Modifier.weight(1f), 40)
        }

        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            SensorCard("Água", s.coolantTempC, "°C", Modifier.weight(1f), high = 100.0)
            SensorCard("Óleo motor", s.oilTempC, "°C", Modifier.weight(1f), high = 115.0)
            SensorCard("Pressão óleo", s.oilPressureBar, "bar", Modifier.weight(1f), low = 1.2)
            SensorCard("Turbo", s.boostBar, "bar", Modifier.weight(1f))
            SensorCard("Câmbio", s.gearboxTempC, "°C", Modifier.weight(1f), high = 105.0)
            SensorCard("Peso", s.combinationWeightKg?.div(1000.0), "t", Modifier.weight(1f))
        }

        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OperationCard(s, Modifier.weight(1.7f))
            DrivingStateCard(s, Modifier.weight(1f))
        }
    }
}

@Composable
private fun GearCard(s: LiveState, modifier: Modifier = Modifier) {
    Card(modifier, colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
        Column(Modifier.fillMaxWidth().padding(14.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text("I-SHIFT", fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(s.currentGear?.toString() ?: "—", fontSize = 52.sp, fontWeight = FontWeight.Black)
            Text("selecionada ${s.selectedGear?.toString() ?: "—"}", fontSize = 12.sp, color = MaterialTheme.colorScheme.secondary)
        }
    }
}

@Composable
private fun OperationCard(s: LiveState, modifier: Modifier) {
    Card(modifier, colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("OPERAÇÃO ATUAL", fontWeight = FontWeight.Bold, fontSize = 13.sp)
            val route = listOfNotNull(s.origin, s.destination).joinToString(" → ").ifBlank { "Viagem em acompanhamento" }
            Text(route, fontWeight = FontWeight.SemiBold, fontSize = 18.sp)
            HorizontalDivider()
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                SmallMetric("Distância", s.tripDistanceKm?.let { "${it.fmt1()} km" } ?: "—")
                SmallMetric("Consumido", s.tripFuelLiters?.let { "${it.fmt1()} L" } ?: "—")
                SmallMetric("Média", s.effectiveAverage()?.let { "${it.fmt2()} km/L" } ?: "—")
                SmallMetric("Carga", s.tripWeightT?.let { "${it.fmt1()} t" } ?: "—")
            }
        }
    }
}

@Composable
private fun DrivingStateCard(s: LiveState, modifier: Modifier) {
    Card(modifier, colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text("CONDUÇÃO", fontWeight = FontWeight.Bold, fontSize = 13.sp)
            StateLine("I-Roll", s.iRollActive)
            StateLine("Cruise", s.cruiseActive)
            StateLine("VEB", s.vebStage?.let { it > 0 }, s.vebStage?.let { "estágio $it" })
            SmallMetric("Pedal", s.acceleratorPct?.let { "${it.fmt0()} %" } ?: "—")
        }
    }
}

@Composable
private fun FuelPage(s: LiveState) {
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Consumo", fontSize = 25.sp, fontWeight = FontWeight.Bold)
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            HeroCard("MÉDIA GERAL DA VIAGEM", s.effectiveAverage()?.fmt2() ?: "—", "km/L", Modifier.weight(1.35f), 50)
            HeroCard("ÚLTIMOS 100 KM", s.last100KmAverageKml?.fmt2() ?: "—", "km/L", Modifier.weight(1.35f), 50)
            HeroCard("CONSUMO AGORA", instantConsumption(s)?.fmt2() ?: "—", "km/L", Modifier.weight(1f), 40)
            HeroCard("TOTAL CONSUMIDO", s.tripFuelLiters?.fmt1() ?: "—", "L", Modifier.weight(1f), 40)
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            MetricPanel("Distância da viagem", s.tripDistanceKm?.let { "${it.fmt1()} km" } ?: "—", Modifier.weight(1f))
            MetricPanel("Vazão de combustível", s.fuelRateLph?.let { "${it.fmt1()} L/h" } ?: "—", Modifier.weight(1f))
            MetricPanel("Eco Score", s.ecoScore?.let { "${it.fmt0()} / 100" } ?: "—", Modifier.weight(1f))
            MetricPanel("Odômetro", s.odometerKm?.let { "${it.fmt1()} km" } ?: "—", Modifier.weight(1f))
        }
        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
            Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Eficiência", fontWeight = FontWeight.Bold)
                Text("Esta primeira versão já respeita a hierarquia que definimos: média geral, últimos 100 km, consumo instantâneo e litros da viagem. Histórico e assistente entram nas próximas versões.")
            }
        }
    }
}

@Composable
private fun TripPage(s: LiveState) {
    val context = LocalContext.current
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Viagem", fontSize = 25.sp, fontWeight = FontWeight.Bold)
        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
            Column(Modifier.fillMaxWidth().padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(listOfNotNull(s.origin, s.destination).joinToString(" → ").ifBlank { "Viagem atual" }, fontSize = 24.sp, fontWeight = FontWeight.Bold)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    SmallMetric("Distância", s.tripDistanceKm?.let { "${it.fmt1()} km" } ?: "—")
                    SmallMetric("Diesel", s.tripFuelLiters?.let { "${it.fmt1()} L" } ?: "—")
                    SmallMetric("Média", s.effectiveAverage()?.let { "${it.fmt2()} km/L" } ?: "—")
                    SmallMetric("Peso cadastrado", s.tripWeightT?.let { "${it.fmt1()} t" } ?: "—")
                    SmallMetric("Odômetro", s.odometerKm?.let { "${it.fmt1()} km" } ?: "—")
                }
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            MetricPanel("GPS", if (s.latitude != null && s.longitude != null) "${s.latitude.fmt5()}, ${s.longitude.fmt5()}" else "Sem posição", Modifier.weight(1f))
            Button(
                onClick = {
                    if (s.latitude != null && s.longitude != null) {
                        val uri = Uri.parse("geo:${s.latitude},${s.longitude}?q=${s.latitude},${s.longitude}(TruckBox)")
                        context.startActivity(Intent(Intent.ACTION_VIEW, uri))
                    }
                },
                enabled = s.latitude != null && s.longitude != null,
                modifier = Modifier.weight(0.45f).height(74.dp),
            ) {
                Icon(Icons.Default.Map, null)
                Spacer(Modifier.width(8.dp))
                Text("ABRIR MAPA")
            }
        }
        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
            Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Operação", fontWeight = FontWeight.Bold)
                Text("A multimídia é somente interface. Início/encerramento de viagem, combustível e histórico continuam preservados pelo Core/Gateway/Cloud; desligar esta tela não interrompe a viagem.")
            }
        }
    }
}

@Composable
private fun SensorsPage(s: LiveState) {
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Text("Sensores", fontSize = 25.sp, fontWeight = FontWeight.Bold)
        Text("Todos os sinais mapeados pelo Core V0.6.2. Quando a ECU publica FF/indisponível, mostramos —.", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)

        val sensors = listOf(
            Triple("Rotação", s.rpm, "rpm"),
            Triple("Pedal acelerador", s.acceleratorPct, "%"),
            Triple("Torque", s.torquePct, "%"),
            Triple("Torque motor", s.torqueNm, "Nm"),
            Triple("Potência", s.powerKw, "kW"),
            Triple("Torque fricção", s.nominalFrictionTorquePct, "%"),
            Triple("Torque freio motor", s.retarderTorquePct, "%"),
            Triple("Pedal freio", s.brakePedalPct, "%"),

            Triple("Água", s.coolantTempC, "°C"),
            Triple("Nível arrefecimento", s.coolantLevelPct, "%"),
            Triple("Pressão arrefecimento", s.coolantPressureBar, "bar"),
            Triple("Óleo motor", s.oilTempC, "°C"),
            Triple("Nível óleo motor", s.oilLevelPct, "%"),
            Triple("Pressão óleo", s.oilPressureBar, "bar"),
            Triple("Pressão cárter", s.crankcasePressureKpa, "kPa"),
            Triple("Temp. combustível", s.fuelTempC, "°C"),
            Triple("Pressão combustível", s.fuelPressureBar, "bar"),

            Triple("Temp. ambiente", s.ambientTempC, "°C"),
            Triple("Ar admissão", s.intakeTempC, "°C"),
            Triple("Pressão entrada ar", s.airInletPressureKpa, "kPa"),
            Triple("Restrição filtro ar", s.airCleanerDiffPressureKpa, "kPa"),
            Triple("Turbo", s.boostBar, "bar"),

            Triple("Óleo câmbio", s.gearboxTempC, "°C"),
            Triple("Slip embreagem", s.clutchSlipPct, "%"),
            Triple("RPM entrada câmbio", s.transInputRpm, "rpm"),
            Triple("RPM saída câmbio", s.transOutputRpm, "rpm"),

            Triple("Velocidade tacógrafo", s.tachoSpeedKmh, "km/h"),
            Triple("RPM saída tacógrafo", s.tachoOutputRpm, "rpm"),
            Triple("Bogie tração", s.driveBogieKg?.div(1000.0), "t"),
            Triple("Eixo tração 1F", s.axle1fWeightKg?.div(1000.0), "t"),
            Triple("Eixo tração 2F", s.axle2fWeightKg?.div(1000.0), "t"),
            Triple("Peso conjunto CAN", s.combinationWeightKg?.div(1000.0), "t"),
            Triple("Vazão diesel", s.fuelRateLph, "L/h"),
            Triple("Odômetro", s.odometerKm, "km"),
        )
        sensors.chunked(4).forEach { row ->
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                row.forEach { (name, value, unit) -> SensorCard(name, value, unit, Modifier.weight(1f)) }
                repeat(4 - row.size) { Spacer(Modifier.weight(1f)) }
            }
        }

        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
            Column(Modifier.fillMaxWidth().padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Estados CAN", fontWeight = FontWeight.Bold, fontSize = 13.sp)
                StateLine("ABS ativo", s.absActive)
                StateLine("Aviso ABS/EBS", s.absAmberWarning, when (s.absAmberWarning) { true -> "ATENÇÃO"; false -> "NORMAL"; null -> "?" })
            }
        }
    }
}

@Composable
private fun DiagnosticsPage(s: LiveState) {
    val alerts = buildList {
        if ((s.coolantTempC ?: 0.0) > 100.0) add("Temperatura da água acima de 100 °C")
        if ((s.oilTempC ?: 0.0) > 115.0) add("Temperatura do óleo acima de 115 °C")
        if ((s.gearboxTempC ?: 0.0) > 105.0) add("Temperatura do câmbio acima de 105 °C")
        if ((s.clutchSlipPct ?: 0.0) > 8.0 && (s.speedKmh ?: 0.0) > 20.0) add("Slip de embreagem elevado")
        if ((s.oilPressureBar ?: 99.0) < 1.2 && (s.rpm ?: 0.0) > 500.0) add("Pressão de óleo baixa")
    }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Manutenção e Diagnóstico", fontSize = 25.sp, fontWeight = FontWeight.Bold)
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            StatusPanel("Comunicação", if (s.online) "OK • ${s.source}" else "SEM CONEXÃO", s.online, Modifier.weight(1f))
            StatusPanel("Motor", if (alerts.none { it.contains("água") || it.contains("óleo") && !it.contains("câmbio") }) "NORMAL" else "ATENÇÃO", alerts.none { it.contains("água") || it.contains("óleo") && !it.contains("câmbio") }, Modifier.weight(1f))
            StatusPanel("Transmissão", if (alerts.none { it.contains("câmbio") || it.contains("embreagem") }) "NORMAL" else "ATENÇÃO", alerts.none { it.contains("câmbio") || it.contains("embreagem") }, Modifier.weight(1f))
        }
        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
            Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Alertas instantâneos", fontWeight = FontWeight.Bold)
                if (alerts.isEmpty()) Text("Nenhum alerta pelos limites operacionais atuais.", color = MaterialTheme.colorScheme.primary)
                else alerts.forEach { Text("• $it", color = MaterialTheme.colorScheme.error) }
            }
        }
        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
            Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Saúde do caminhão", fontWeight = FontWeight.Bold)
                Text("Nesta v0.1 os alertas são instantâneos. Tendência, degradação e Health Score continuam sendo calculados pela Cloud e serão trazidos para esta tela depois.")
            }
        }
    }
}

@Composable
private fun SettingsPage(coreHost: String, onSave: (String) -> Unit, live: LiveState) {
    var text by remember(coreHost) { mutableStateOf(coreHost) }
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Configurações", fontSize = 25.sp, fontWeight = FontWeight.Bold)
        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
            Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text("Conexão com o Core", fontWeight = FontWeight.Bold)
                OutlinedTextField(
                    value = text,
                    onValueChange = { text = it },
                    label = { Text("IP ou host do Core") },
                    supportingText = { Text("Ex.: 192.168.4.1 ou truckbox.local") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                Button(onClick = { onSave(text) }) { Text("SALVAR E RECONECTAR") }
                Text("Fonte atual: ${live.source}")
                if (live.error != null) Text(live.error, color = MaterialTheme.colorScheme.error)
            }
        }
        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
            Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Arquitetura", fontWeight = FontWeight.Bold)
                Text("Core → dados ao vivo. Cloud → fallback e histórico. Multimídia → visualização. Este aplicativo não assume a função de Gateway e não precisa ficar ligado para a TruckBox continuar registrando.")
            }
        }
    }
}

@Composable
private fun HeroCard(title: String, value: String, unit: String, modifier: Modifier = Modifier, valueSize: Int = 44) {
    Card(modifier, colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
        Column(Modifier.fillMaxWidth().padding(14.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(title, fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
            Text(value, fontSize = valueSize.sp, fontWeight = FontWeight.Black, maxLines = 1)
            Text(unit, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun SensorCard(title: String, value: Double?, unit: String, modifier: Modifier = Modifier, low: Double? = null, high: Double? = null) {
    val alert = value != null && ((low != null && value < low) || (high != null && value > high))
    Card(
        modifier,
        colors = CardDefaults.cardColors(containerColor = if (alert) MaterialTheme.colorScheme.errorContainer else Color(0xFF151A20))
    ) {
        Column(Modifier.fillMaxWidth().padding(12.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(title, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
            Text(value?.let { if (kotlin.math.abs(it) >= 100) it.fmt0() else it.fmt1() } ?: "—", fontSize = 24.sp, fontWeight = FontWeight.Bold)
            Text(unit, fontSize = 11.sp)
        }
    }
}

@Composable
private fun MetricPanel(title: String, value: String, modifier: Modifier = Modifier) {
    Card(modifier, colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
        Column(Modifier.fillMaxWidth().padding(14.dp)) {
            Text(title, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(6.dp))
            Text(value, fontSize = 23.sp, fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
private fun StatusPanel(title: String, value: String, ok: Boolean, modifier: Modifier = Modifier) {
    Card(modifier, colors = CardDefaults.cardColors(containerColor = Color(0xFF151A20))) {
        Column(Modifier.fillMaxWidth().padding(16.dp)) {
            Text(title, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(value, fontSize = 22.sp, fontWeight = FontWeight.Bold, color = if (ok) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error)
        }
    }
}

@Composable
private fun SmallMetric(label: String, value: String) {
    Column {
        Text(label, fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, fontSize = 16.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun StateLine(label: String, active: Boolean?, detail: String? = null) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier.width(10.dp).height(10.dp).background(
                when (active) {
                    true -> MaterialTheme.colorScheme.primary
                    false -> MaterialTheme.colorScheme.onSurfaceVariant
                    null -> Color.DarkGray
                }, RoundedCornerShape(5.dp)
            )
        )
        Spacer(Modifier.width(8.dp))
        Text(label, Modifier.weight(1f))
        Text(detail ?: when (active) { true -> "ATIVO"; false -> "—"; null -> "?" }, fontWeight = FontWeight.Bold, fontSize = 12.sp)
    }
}

private fun LiveState.effectiveAverage(): Double? = tripAverageKml ?: if (tripDistanceKm != null && tripFuelLiters != null && tripFuelLiters > 0.01) tripDistanceKm / tripFuelLiters else null

private fun instantConsumption(s: LiveState): Double? {
    val speed = s.speedKmh ?: return null
    val rate = s.fuelRateLph ?: return null
    return if (speed > 1.0 && rate > 0.01) speed / rate else null
}

private class LiveRepository(private val configuredHost: String) {
    private val cloudUrl = "https://wbzrrjufhqfgoctxtlyi.supabase.co/functions/v1/truckbox-dashboard-data?key=tbx-public-ro-20260828-7f2c91"

    fun fetch(): LiveState? {
        fetchCore()?.let { return it }
        return fetchCloud()
    }

    private fun fetchCore(): LiveState? {
        val bases = linkedSetOf<String>()
        fun add(raw: String) {
            if (raw.isBlank()) return
            bases += if (raw.startsWith("http://") || raw.startsWith("https://")) raw.trimEnd('/') else "http://${raw.trimEnd('/')}"
        }
        add(configuredHost)
        add("192.168.4.1")
        add("truckbox.local")
        for (base in bases) {
            try {
                val json = getJson("$base/api/state?nocache=${System.currentTimeMillis()}", 1200, 1800) ?: continue
                return parse(json, "CORE")
            } catch (_: Throwable) { }
        }
        return null
    }

    private fun fetchCloud(): LiveState? = try {
        getJson(cloudUrl, 2500, 3500)?.let { parse(it, "CLOUD") }
    } catch (_: Throwable) { null }

    private fun getJson(url: String, connectTimeout: Int, readTimeout: Int): JSONObject? {
        val c = (URL(url).openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            this.connectTimeout = connectTimeout
            this.readTimeout = readTimeout
            useCaches = false
            setRequestProperty("Accept", "application/json")
            setRequestProperty("Connection", "close")
        }
        return try {
            if (c.responseCode !in 200..299) return null
            val text = c.inputStream.bufferedReader().use { it.readText() }
            if (text.isBlank()) null else JSONObject(text)
        } finally {
            c.disconnect()
        }
    }

    private fun parse(j: JSONObject, source: String): LiveState {
        val tripDistance = j.number("tripDistanceKm", "trip_distance_km", "distanceKm", "distance_km", "live_distance_km", "sessionDistanceKm")
        val tripFuel = j.number("tripFuelLiters", "trip_fuel_liters", "fuelLiters", "fuel_liters", "live_fuel_liters", "sessionLiters")
        val avg = j.number("tripAverageKml", "trip_average_kml", "averageKml", "average_kml", "avgKml", "sessionAverageKml")
            ?: if (tripDistance != null && tripFuel != null && tripFuel > 0.01) tripDistance / tripFuel else null
        return LiveState(
            online = true,
            source = source,
            updatedAtMs = System.currentTimeMillis(),
            speedKmh = j.number("speedKph", "speedKmh", "speed_kmh", "vehicleSpeedKph"),
            rpm = j.number("rpm", "engineRpm", "engine_rpm"),
            acceleratorPct = j.number("acceleratorPct", "accelerator_pedal_pct", "pedalPct", "pedal_pct"),
            torquePct = j.number("actualTorquePct", "torquePct", "engine_torque_pct", "actual_torque_pct"),
            torqueNm = j.number("torqueNm", "torque_nm"),
            powerKw = j.number("powerKw", "power_kw"),
            nominalFrictionTorquePct = j.number("nominalFrictionTorquePct", "nominal_friction_torque_pct"),
            retarderTorquePct = j.number("retarderTorquePct", "retarder_torque_pct"),
            currentGear = j.integer("currentGear", "current_gear", "gear"),
            selectedGear = j.integer("selectedGear", "selected_gear"),
            coolantTempC = j.number("coolantTempC", "coolant_temp_c", "waterTempC", "engineCoolantTempC"),
            oilTempC = j.number("oilTempC", "oil_temp_c", "engineOilTempC", "engine_oil_temp_c"),
            fuelTempC = j.number("fuelTempC", "fuel_temp_c"),
            ambientTempC = j.number("ambientTempC", "ambient_temp_c"),
            intakeTempC = j.number("intakeAirTempC", "intake_temp_c", "intakeTempC"),
            oilLevelPct = j.number("oilLevelPct", "oil_level_pct"),
            coolantLevelPct = j.number("coolantLevelPct", "coolant_level_pct"),
            oilPressureBar = j.number("oilPressureBar", "oil_pressure_bar", "engineOilPressureBar"),
            fuelPressureBar = j.number("fuelPressureBar", "fuel_pressure_bar"),
            coolantPressureBar = j.number("coolantPressureBar", "coolant_pressure_bar"),
            crankcasePressureKpa = j.number("crankcasePressureKpa", "crankcase_pressure_kpa"),
            airInletPressureKpa = j.number("airInletPressureKpa", "air_inlet_pressure_kpa"),
            airCleanerDiffPressureKpa = j.number("airCleanerDiffPressureKpa", "air_cleaner_diff_pressure_kpa"),
            boostBar = j.number("boostBar", "boost_bar", "turboBar", "turbo_pressure_bar"),
            gearboxTempC = j.number("gearboxTempC", "gearbox_temp_c", "transmissionOilTempC", "transmission_temp_c"),
            clutchSlipPct = j.number("clutchSlipPct", "clutch_slip_pct"),
            transInputRpm = j.number("transInputRpm", "trans_input_rpm"),
            transOutputRpm = j.number("transOutputRpm", "trans_output_rpm"),
            brakePedalPct = j.number("brakePedalPct", "brake_pedal_pct"),
            tachoSpeedKmh = j.number("tachoSpeedKmh", "tacho_speed_kmh"),
            tachoOutputRpm = j.number("tachoOutputRpm", "tacho_output_rpm"),
            absActive = j.boolean("absActive", "abs_active"),
            absAmberWarning = j.boolean("absAmberWarning", "abs_amber_warning"),
            combinationWeightKg = j.number("combinationWeightKg", "combination_weight_kg", "total_weight_can_kg"),
            driveBogieKg = j.number("driveBogieKg", "drive_bogie_kg", "bogie_weight_kg"),
            axle1fWeightKg = j.number("axle1fWeightKg", "axle_1f_weight_kg"),
            axle2fWeightKg = j.number("axle2fWeightKg", "axle_2f_weight_kg"),
            odometerKm = j.number("distanceTotalKm", "odometerKm", "odometer_km", "totalDistanceKm"),
            coreFuelLiters = j.number("fuelLitersTest", "coreFuelLiters", "core_fuel_l", "fuelTotalLiters"),
            fuelRateLph = j.number("fuelRateLph", "fuel_rate_lph"),
            tripDistanceKm = tripDistance,
            tripFuelLiters = tripFuel,
            tripAverageKml = avg,
            last100KmAverageKml = j.number("last100KmAverageKml", "last_100_km_average_kml", "rolling100Kml"),
            ecoScore = j.number("ecoScore", "eco_score"),
            iRollActive = j.boolean("iRollActive", "i_roll_active", "iRoll"),
            cruiseActive = j.boolean("cruiseActive", "cruise_active", "cruise"),
            vebStage = j.integer("vebStage", "veb_stage", "vebStageRaw"),
            origin = j.text("origin", "trip_origin", "origin_name"),
            destination = j.text("destination", "trip_destination", "destination_name"),
            tripWeightT = j.number("tripWeightT", "weightT", "weight_t", "cargo_weight_t"),
            latitude = j.number("lat", "latitude", "gps_lat"),
            longitude = j.number("lon", "longitude", "gps_lon"),
        )
    }
}

private fun JSONObject.findValue(keys: Set<String>, depth: Int = 0): Any? {
    if (depth > 7) return null
    val names = keys.map { it.lowercase(Locale.US) }.toSet()
    val iter = keys()
    while (iter.hasNext()) {
        val key = iter.next()
        if (key.lowercase(Locale.US) in names && !isNull(key)) return opt(key)
    }
    val nested = keys()
    while (nested.hasNext()) {
        val key = nested.next()
        when (val v = opt(key)) {
            is JSONObject -> v.findValue(keys, depth + 1)?.let { return it }
            is JSONArray -> {
                for (i in 0 until minOf(v.length(), 20)) {
                    val item = v.opt(i)
                    if (item is JSONObject) item.findValue(keys, depth + 1)?.let { return it }
                }
            }
        }
    }
    return null
}

private fun JSONObject.number(vararg keys: String): Double? {
    val v = findValue(keys.toSet()) ?: return null
    return when (v) {
        is Number -> v.toDouble().takeIf { it.isFinite() }
        is String -> v.replace(',', '.').toDoubleOrNull()?.takeIf { it.isFinite() }
        else -> null
    }
}

private fun JSONObject.integer(vararg keys: String): Int? = number(*keys)?.toInt()

private fun JSONObject.boolean(vararg keys: String): Boolean? {
    val v = findValue(keys.toSet()) ?: return null
    return when (v) {
        is Boolean -> v
        is Number -> v.toInt() != 0
        is String -> when (v.lowercase(Locale.US)) {
            "true", "1", "on", "active", "ativo" -> true
            "false", "0", "off", "inactive", "inativo" -> false
            else -> null
        }
        else -> null
    }
}

private fun JSONObject.text(vararg keys: String): String? {
    val v = findValue(keys.toSet()) ?: return null
    val text = v.toString().trim()
    return text.takeIf { it.isNotBlank() && it != "null" }
}

private fun Double.fmt0(): String = String.format(Locale.US, "%.0f", this)
private fun Double.fmt1(): String = String.format(Locale.US, "%.1f", this)
private fun Double.fmt2(): String = String.format(Locale.US, "%.2f", this)
private fun Double.fmt5(): String = String.format(Locale.US, "%.5f", this)
