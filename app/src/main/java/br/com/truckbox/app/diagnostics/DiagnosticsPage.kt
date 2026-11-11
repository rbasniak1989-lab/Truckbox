package br.com.truckbox.app.diagnostics

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import br.com.truckbox.app.operations.TripRecord
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.time.Instant

@Composable
fun DiagnosticsPage(client: DiagnosticClient, trips: List<TripRecord>) {
    var tripUid by remember { mutableStateOf<String?>(null) }
    var snapshot by remember { mutableStateOf<JSONObject?>(null) }
    var operating by remember { mutableStateOf<JSONObject?>(null) }
    var menu by remember { mutableStateOf(false) }; var inactive by remember { mutableStateOf(false) }
    var refresh by remember { mutableIntStateOf(0) }; var beforeId by remember { mutableStateOf<String?>(null) }; var faultOffset by remember { mutableIntStateOf(0) }
    LaunchedEffect(tripUid, refresh, beforeId, faultOffset) {
        while (isActive) {
            val result = withContext(Dispatchers.IO) { client.snapshot(tripUid, beforeId, faultOffset) to client.local("/api/volvo") }
            snapshot = result.first; operating = result.second ?: result.first.optJSONObject("operating"); delay(30000)
        }
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text("Diagnóstico J1708", style = MaterialTheme.typography.headlineSmall)
        Text("Fonte: ${snapshot?.optString("source") ?: "consultando"}. Descrições candidatas; códigos desconhecidos não recebem gravidade automática.")
        Box {
            OutlinedButton(onClick = { menu = true }) { Text(tripUid?.let { "Viagem ${it.take(8)}" } ?: "Todas as observações disponíveis") }
            DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                DropdownMenuItem(text = { Text("Todas") }, onClick = { tripUid = null; beforeId = null; faultOffset = 0; menu = false })
                trips.take(30).forEach { t -> DropdownMenuItem(text = { Text("${t.origin} → ${t.destination}") }, onClick = { tripUid = t.clientUid; beforeId = null; faultOffset = 0; menu = false }) }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            FilterChip(selected = !inactive, onClick = { inactive = false }, label = { Text("Ativas") })
            FilterChip(selected = inactive, onClick = { inactive = true }, label = { Text("Inativas / histórico") })
            TextButton(onClick = { beforeId = null; faultOffset = 0; refresh++ }) { Text("Atualizar") }
        }
        if (snapshot?.optBoolean("ok") == false) Text("Consulta indisponível. Verifique a versão do Core ou a disponibilidade do serviço Cloud; isso não significa ausência de falhas.")
        val faults = snapshot?.optJSONArray("faults")
        val visible = (0 until (faults?.length() ?: 0)).mapNotNull { faults?.optJSONObject(it) }
            .filter { if (inactive) it.optString("state") != "ACTIVE" else it.optString("state") == "ACTIVE" }
        if (visible.isEmpty()) Text("Nenhum código nesta consulta. Ausência de transmissão não confirma resolução de falhas.")
        visible.forEach { fault ->
            val d = fault.optJSONObject("definition") ?: JSONObject()
            ElevatedCard(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                    Text("${d.optString("module", "Módulo")} • MID ${fault.optInt("mid")} ${fault.optString("identifier_type")} ${fault.optInt("identifier")} FMI ${fault.optInt("fmi")}", style = MaterialTheme.typography.titleMedium)
                    Text(when (fault.optString("state")) { "ACTIVE" -> "Ativa observada"; "INACTIVE" -> "Inativa informada pela ECU"; else -> "Estado desconhecido" })
                    Text(d.optString("description_pt", d.optString("generic_fmi_pt", "Significado OEM desconhecido")))
                    if (d.has("driver_pt")) Text(d.optString("driver_pt"))
                    Text("Correspondência: ${d.optString("confidence", "UNKNOWN")}")
                    Text("Contador ECU: ${if (fault.isNull("occurrences")) "não informado" else fault.optInt("occurrences").toString()}")
                    fun time(key: String): String {
                        val objectTime = fault.optJSONObject(key)
                        if (objectTime != null) return if (!objectTime.isNull("unix_ms"))
                            runCatching { Instant.ofEpochMilli(objectTime.getLong("unix_ms")).toString() }.getOrDefault("desconhecido")
                            else "boot ${objectTime.optLong("boot")}, ${objectTime.optLong("uptime_ms")} ms (sem UTC)"
                        val at = fault.optJSONArray(key) ?: return "desconhecido"
                        return if (!at.isNull(2)) runCatching { Instant.ofEpochMilli(at.getLong(2)).toString() }.getOrDefault("desconhecido")
                        else "boot ${at.optLong(0)}, ${at.optLong(1)} ms (sem UTC)"
                    }
                    Text("Primeira observação: ${time("first")}")
                    Text("Última observação: ${time("last")}")
                    val checks = d.optJSONArray("recommended_checks")
                    for (i in 0 until (checks?.length() ?: 0)) Text("• ${checks?.optString(i)}")
                    Text("Bytes: ${fault.optString("parameter_hex")}")
                }
            }
        }
        val nextFault = snapshot?.takeUnless { it.isNull("next_fault_offset") }?.optInt("next_fault_offset")
        val nextPage = snapshot?.takeUnless { it.isNull("next_before_id") }?.optString("next_before_id")?.takeIf { it.isNotBlank() }
        if (nextFault != null || nextPage != null) OutlinedButton(onClick = {
            if (nextFault != null) faultOffset = nextFault else { beforeId = nextPage; faultOffset = 0 }
        }) { Text("Carregar mais histórico") }
        operating?.let { state ->
            Text("Última leitura disponível de operação; dados Cloud podem estar atrasados.")
            Text("I-Roll provável: ${if (state.optBoolean("iroll_probable")) "sim" else "não detectado"}")
            Text("VEB: ${if (state.isNull("veb_pct")) "sem leitura recente" else "${state.optDouble("veb_pct")}%"}")
            Text("Diferença motor/eixo de entrada: ${if (state.isNull("engine_input_rpm_difference")) "sem leituras suficientes" else "${state.optDouble("engine_input_rpm_difference")} rpm"}")
            Text("Esses sinais não confirmam patinação mecânica.")
        }
        val reference = snapshot?.optJSONObject("counter_reference")
        if (reference != null) {
            Text("Consumo no intervalo ECU: ${if (reference.isNull("fuel_l")) "sem extremidades suficientes; usar integração/estimativa identificada" else "${reference.optDouble("fuel_l")} L"}")
            Text(if (reference.optBoolean("financial_primary_allowed")) "Extremidades próximas aos eventos; escala em validação. Valores não foram alterados no cadastro da viagem."
                else "Limites ausentes, tardios ou incertos: referência estimada; conservar integração e estimativas identificadas.")
        }
        Text("Histórico por páginas e cache limitado. A biblioteca detalhada na nuvem depende da publicação do serviço de diagnóstico.")
    }
}
