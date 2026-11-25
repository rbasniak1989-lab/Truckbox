package br.com.truckbox.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.repeatOnLifecycle
import br.com.truckbox.app.cloud.DriverCloudException
import br.com.truckbox.app.cloud.RouteOption
import br.com.truckbox.app.cloud.TripRoute
import br.com.truckbox.app.cloud.TripRoutes
import br.com.truckbox.app.cloud.TripRoutesClient
import br.com.truckbox.app.cloud.TruckRouteProfile
import br.com.truckbox.app.operations.TripRecord
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.text.DateFormat
import java.text.NumberFormat
import java.util.Date
import java.util.Locale
import kotlin.math.cos
import kotlin.math.min

private enum class RouteDialog { OPTIONS, PROFILE, DETAILS, CONFIRM }
private val brazil = Locale.forLanguageTag("pt-BR")

@Composable
internal fun TripRoutePanel(trip: TripRecord, client: TripRoutesClient, ending: Boolean) {
    val scope = rememberCoroutineScope()
    val requests = remember { Mutex() }
    var routes by remember { mutableStateOf<TripRoutes?>(null) }
    var selection by remember { mutableStateOf<RouteOption?>(null) }
    var preview by remember { mutableStateOf<TripRoute?>(null) }
    var details by remember { mutableStateOf<TripRoute?>(null) }
    var dialog by remember { mutableStateOf<RouteDialog?>(null) }
    var busy by remember { mutableStateOf(false) }
    var fromGps by remember { mutableStateOf(false) }
    var profileInput by rememberSaveable { mutableStateOf<List<String>>(arrayListOf("", "", "", "")) }
    var message by remember { mutableStateOf<String?>(null) }
    var fetchError by remember { mutableStateOf<String?>(null) }

    suspend fun refresh() {
        try {
            val latest = withContext(Dispatchers.IO) { client.list(trip) }
            routes = latest
            fetchError = null
            selection = selection?.let { selected -> latest.options.firstOrNull { it.id == selected.id && it.provider == selected.provider } }
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            fetchError = routeError(e)
        }
    }

    fun submit(request: suspend () -> Unit) {
        busy = true
        message = null
        scope.launch {
            try {
                requests.withLock { request() }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                message = routeError(e)
            } finally {
                busy = false
            }
        }
    }

    val lifecycle = LocalLifecycleOwner.current.lifecycle
    LaunchedEffect(lifecycle, trip.cloudId, ending) {
        if (trip.cloudId != null && !ending) {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                while (true) {
                    requests.withLock { refresh() }
                    delay(15_000)
                }
            }
        }
    }

    fun showProfile(useGps: Boolean) {
        fromGps = useGps
        dialog = RouteDialog.PROFILE
    }

    val linked = routes?.linked
    val selected = selection
    val available = trip.cloudId != null && !ending
    ElevatedCard(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Rota da viagem", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
            if (!available) {
                Text(if (ending) "Encerramento aguardando confirmação da Cloud."
                    else "Aguarde a confirmação da viagem pela Cloud para vincular uma rota.")
                return@Column
            }
            fetchError?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            message?.let { Text(it) }
            if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
            if (linked == null) {
                Text(if (routes == null) "Consultando rota na Cloud" else "Sem rota vinculada")
                Button({ showProfile(false) }, Modifier.fillMaxWidth(), enabled = !busy) { Text("CALCULAR ROTA") }
                OutlinedButton({ dialog = RouteDialog.OPTIONS }, Modifier.fillMaxWidth(), enabled = !busy) { Text("SELECIONAR ROTA") }
            } else {
                Text("${linked.option.kind()} vinculada")
                Text(linked.option.summary(trip))
                Button({
                    details = linked
                    dialog = RouteDialog.DETAILS
                }, Modifier.fillMaxWidth(), enabled = !busy) { Text("VISUALIZAR ROTA") }
                OutlinedButton({ dialog = RouteDialog.OPTIONS }, Modifier.fillMaxWidth(), enabled = !busy) { Text("ALTERAR ROTA") }
                OutlinedButton({ showProfile(true) }, Modifier.fillMaxWidth(), enabled = !busy) { Text("RECALCULAR A PARTIR DA POSIÇÃO ATUAL") }
            }
            if (selected != null) {
                Text("Selecionada · ${selected.kind()}")
                Text(selected.summary(trip))
                preview?.takeIf { it.option.id == selected.id }?.let { planned ->
                    TextButton({ details = planned; dialog = RouteDialog.DETAILS }, enabled = !busy) { Text("VER TRAÇADO E PEDÁGIOS") }
                }
            }
            if (linked == null || selected != null && selected.id != linked.option.id) {
                Button({
                    dialog = RouteDialog.CONFIRM
                }, Modifier.fillMaxWidth(), enabled = selected != null && !busy) { Text("VINCULAR À VIAGEM ATUAL") }
            }
        }
    }

    if (!available) return
    when (dialog) {
        RouteDialog.PROFILE -> ProfileDialog(profileInput, fromGps, trip.weightT, {
            dialog = null
        }) { input, profile, useGps ->
            profileInput = input
            dialog = null
            submit {
                val calculated = withContext(Dispatchers.IO) { client.calculate(trip, profile, useGps) }
                preview = calculated
                refresh()
                selection = calculated.option
                details = calculated
                dialog = RouteDialog.DETAILS
            }
        }
        RouteDialog.OPTIONS -> AlertDialog(
            onDismissRequest = { dialog = null },
            title = { Text("Selecionar rota") },
            text = {
                Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    val options = routes?.options.orEmpty()
                    if (options.isEmpty()) Text("Nenhuma opção disponível. Calcule uma rota para esta viagem.")
                    options.forEach { option ->
                        val choose = { selection = option; dialog = null }
                        Row(Modifier.fillMaxWidth().clickable(onClick = choose).padding(vertical = 6.dp),
                            verticalAlignment = Alignment.CenterVertically) {
                            RadioButton(selected = selected?.id == option.id, onClick = null)
                            Spacer(Modifier.width(8.dp))
                            Column {
                                Text(option.kind(), fontWeight = FontWeight.Bold)
                                Text(option.summary(trip))
                                if (option.id == linked?.option?.id) Text("Vinculada", style = MaterialTheme.typography.labelMedium)
                            }
                        }
                    }
                }
            },
            confirmButton = { TextButton({ showProfile(false) }) { Text("CALCULAR NOVA ROTA") } },
            dismissButton = { TextButton({ dialog = null }) { Text("VOLTAR") } },
        )
        RouteDialog.DETAILS -> details?.let { planned -> RouteDetails(planned, trip) { dialog = null } }
        RouteDialog.CONFIRM -> selected?.let { chosen ->
            AlertDialog(
                onDismissRequest = { dialog = null },
                title = { Text("Vincular à viagem atual?") },
                text = {
                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text("${trip.origin} → ${trip.destination}")
                        Text("${chosen.kind()} · ${chosen.summary(trip)}")
                        if (chosen.provider == "GOOGLE") Text("O plano salvo pelo Google usa o perfil carro.")
                    }
                },
                confirmButton = {
                    TextButton({
                        dialog = null
                        submit {
                            withContext(Dispatchers.IO) { client.attach(trip, chosen) }
                            message = "Vinculação confirmada pela Cloud."
                            selection = null
                            refresh()
                        }
                    }) { Text("VINCULAR") }
                },
                dismissButton = { TextButton({ dialog = null }) { Text("VOLTAR") } },
            )
        }
        null -> Unit
    }
}

@Composable
private fun ProfileDialog(
    input: List<String>, fromGps: Boolean, cargoT: Double?, dismiss: () -> Unit,
    calculate: (ArrayList<String>, TruckRouteProfile, Boolean) -> Unit,
) {
    var height by rememberSaveable { mutableStateOf(input[0]) }
    var width by rememberSaveable { mutableStateOf(input[1]) }
    var gross by rememberSaveable { mutableStateOf(input[2]) }
    var current by rememberSaveable { mutableStateOf(input[3]) }
    var gps by rememberSaveable { mutableStateOf(fromGps) }
    val profile = TruckRouteProfile.fromInput(height, width, gross, current)
        ?.takeIf { cargoT == null || it.weightKg > cargoT * 1_000.0 }
    AlertDialog(
        onDismissRequest = dismiss,
        title = { Text("Calcular rota para caminhão") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(7.dp)) {
                Text("Rodotrem de 25 m · 9 eixos")
                ProfileField(height, { height = it }, "Altura total em metros")
                ProfileField(width, { width = it }, "Largura total em metros")
                ProfileField(gross, { gross = it }, "PBTC permitido em toneladas")
                ProfileField(current, { current = it }, "Peso bruto atual em toneladas")
                Text("O peso bruto atual inclui o conjunto, a carga e os demais pesos a bordo.", style = MaterialTheme.typography.bodySmall)
                if (listOf(height, width, gross, current).all { it.isNotBlank() } && profile == null) {
                    Text("Confira as dimensões e os pesos. O peso bruto precisa superar a carga e caber no PBTC.",
                        color = MaterialTheme.colorScheme.error)
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Switch(gps, { gps = it })
                    Spacer(Modifier.width(8.dp))
                    Text("Partir da posição atual")
                }
            }
        },
        confirmButton = { TextButton({ profile?.let { calculate(arrayListOf(height, width, gross, current), it, gps) } },
            enabled = profile != null) { Text("CALCULAR") } },
        dismissButton = { TextButton(dismiss) { Text("VOLTAR") } },
    )
}

@Composable
private fun ProfileField(value: String, change: (String) -> Unit, label: String) {
    OutlinedTextField(value, change, Modifier.fillMaxWidth(), label = { Text(label) }, singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal))
}

@Composable
private fun RouteDetails(route: TripRoute, trip: TripRecord, dismiss: () -> Unit) {
    val option = route.option
    AlertDialog(
        onDismissRequest = dismiss,
        title = { Text(option.kind()) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(option.summary(trip))
                RouteDrawing(route.polyline)
                if (option.remaining) {
                    val arrival = Date.from(route.createdAt.plusSeconds(option.durationSec.toLong()))
                    Text("Chegada estimada no cálculo · ${DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT, brazil).format(arrival)}")
                }
                route.validUntil?.let { date ->
                    Text("Proposta disponível até ${DateFormat.getDateInstance(DateFormat.SHORT, brazil).format(Date.from(date))}")
                }
                if (trip.routeSimulationId == option.id && option.provider == "GOOGLE") {
                    trip.plannedFuelLiters?.let { Text("Diesel previsto · ${number(it)} L") }
                    trip.plannedAscentM?.let { Text("Subidas acumuladas · ${number(it)} m") }
                    trip.plannedDescentM?.let { Text("Descidas acumuladas · ${number(it)} m") }
                } else {
                    trip.averageKml?.takeIf { it.isFinite() && it > 0 }?.let { average ->
                        Text("Diesel estimado pela média da viagem · ${number(option.distanceM / 1000.0 / average)} L")
                    }
                }
                option.warnings.forEach { Text(it, color = MaterialTheme.colorScheme.error) }
                Text("Pedágios", fontWeight = FontWeight.Bold)
                when (route.tollCoverage) {
                    "ESTIMATED" -> Text(if (route.tolls.isEmpty()) "Sem pedágios informados" else "Tarifas estimadas pelo provedor")
                    "PARTIAL" -> Text("Informações parciais do provedor")
                    else -> Text("Cobertura tarifária indisponível")
                }
                route.tolls.forEach { toll ->
                    val lower = toll.lowCents
                    val higher = toll.highCents
                    val price = when {
                        lower == null -> "Valor indisponível"
                        higher == null || higher == lower -> currency(lower)
                        else -> "${currency(lower)} a ${currency(higher)}"
                    }
                    Text(toll.name + if (toll.freeFlow) " · Fluxo livre" else "")
                    Text(listOfNotNull(toll.atM?.let { "km ${number(it / 1000.0)} da rota" }, price).joinToString(" · "),
                        style = MaterialTheme.typography.bodySmall)
                }
            }
        },
        confirmButton = { TextButton(dismiss) { Text("VOLTAR") } },
    )
}

private fun RouteOption.kind() = if (remaining) "Rota restante" else "Rota completa"

private fun RouteOption.summary(trip: TripRecord): String {
    val minutes = (durationSec + 59) / 60
    val duration = if (minutes < 60) "${minutes} min" else "${minutes / 60} h ${minutes % 60} min"
    val profile = if (provider == "HERE") "HERE · Caminhão" else "Google · Carro"
    return "${origin ?: trip.origin} → ${destination ?: trip.destination} · ${number(distanceM / 1000.0)} km · $duration · $profile"
}

private fun number(value: Double) = String.format(brazil, "%.1f", value)
private fun currency(cents: Int) = NumberFormat.getCurrencyInstance(brazil).format(cents / 100.0)

private fun routeError(error: Exception): String {
    val cloud = error as? DriverCloudException
    return when (cloud?.code) {
        "fresh_gps_required" -> "A Cloud precisa de uma posição recente do GPS do celular. Confira o envio em Status."
        "driver_trip_changed" -> "A viagem ativa mudou. Aguarde a sincronização antes de vincular."
        "driver_plan_unavailable" -> "A rota expirou ou saiu da lista. Selecione outra ou calcule novamente."
        "invalid_truck_profile" -> "Confira dimensões, PBTC e peso bruto atual."
        "truck_route_restriction" -> "O HERE retornou restrições para o perfil informado."
        "route_not_found", "route_address_not_found" -> "O HERE não encontrou um trajeto para os pontos informados."
        "truck_routing_not_configured" -> "O roteamento HERE está indisponível na Cloud."
        "driver_route_limit" -> "Limite temporário de cálculos atingido. Você ainda pode selecionar uma rota salva."
        else -> if (cloud?.status in listOf(401, 403)) "Confira o código privado do Motorista em Config."
            else "Rota indisponível. Confira a conexão com a Cloud e tente novamente."
    }
}

@Composable
private fun RouteDrawing(polyline: String) {
    val points = remember(polyline) { decodeRoutePolyline(polyline) }
    if (points.isEmpty()) return
    val routeColor = MaterialTheme.colorScheme.primary
    val endColor = MaterialTheme.colorScheme.tertiary
    Canvas(Modifier.fillMaxWidth().height(150.dp).semantics { contentDescription = "Traçado da rota selecionada" }) {
        val minX = points.minOf { it.first }
        val minY = points.minOf { it.second }
        val longitudeScale = cos(Math.toRadians(minY))
        val width = (points.maxOf { it.first } - minX) * longitudeScale
        val height = points.maxOf { it.second } - minY
        val margin = 9.dp.toPx()
        val scale = min((size.width - margin * 2) / width.coerceAtLeast(0.00001),
            (size.height - margin * 2) / height.coerceAtLeast(0.00001))
        fun project(point: Pair<Double, Double>) = Offset(
            ((size.width - width * scale) / 2 + (point.first - minX) * longitudeScale * scale).toFloat(),
            ((size.height + height * scale) / 2 - (point.second - minY) * scale).toFloat(),
        )
        val path = Path()
        val start = project(points.first())
        path.moveTo(start.x, start.y)
        for (point in points.drop(1)) project(point).let { path.lineTo(it.x, it.y) }
        drawPath(path, routeColor, style = Stroke(3.dp.toPx()))
        drawCircle(routeColor, 5.dp.toPx(), start)
        drawCircle(endColor, 5.dp.toPx(), project(points.last()))
    }
}

internal fun decodeRoutePolyline(encoded: String): List<Pair<Double, Double>> {
    if (encoded.length !in 4..42_000) return emptyList()
    var cursor = 0
    fun delta(): Long {
        var value = 0L
        for (shift in 0..30 step 5) {
            require(cursor < encoded.length)
            val digit = encoded[cursor++].code - 63
            require(digit in 0..63)
            value = value or ((digit and 31).toLong() shl shift)
            if (digit < 32) return (value shr 1) xor -(value and 1)
        }
        throw IllegalArgumentException("Invalid route geometry")
    }
    return try {
        var lat = 0L
        var lon = 0L
        buildList {
            while (cursor < encoded.length) {
                lat += delta()
                lon += delta()
                require(lat in -9_000_000L..9_000_000L && lon in -18_000_000L..18_000_000L)
                add(lon / 100_000.0 to lat / 100_000.0)
            }
        }.takeIf { it.size > 1 }.orEmpty()
    } catch (_: IllegalArgumentException) {
        emptyList()
    }
}
