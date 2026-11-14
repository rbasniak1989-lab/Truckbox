package br.com.truckbox.multimedia.next.domain

import java.time.Instant

enum class CoreIssue(val label: String) {
    NO_WIFI("Wi-Fi indisponível para o aplicativo"),
    DNS("Nome do Core não resolvido"),
    TIMEOUT("Tempo de resposta esgotado"),
    HTTP("Resposta HTTP recusada"),
    FORMAT("Resposta fora do formato JSON esperado"),
    IO("Falha de comunicação com o Core"),
}

data class CoreAttempt(val host: String, val issue: CoreIssue? = null, val httpStatus: Int? = null, val resource: String = "/api/state")

/** Apenas metadados da consulta local. Nunca contém payload, token ou identificador do veículo. */
data class CoreDiagnostics(
    val checkedAt: Instant,
    val attempts: List<CoreAttempt>,
    val recognizedValues: Int = 0,
    val sensorApi: Boolean = false,
    val validValues: Int = 0,
    val provisionalValues: Int = 0,
) {
    val responded: Boolean get() = attempts.lastOrNull()?.let { it.issue == null && it.httpStatus in 200..299 } == true
    val explanation: String get() = when {
        responded && sensorApi -> "$recognizedValues sinais mapeados: $validValues com qualidade válida e $provisionalValues não verificados. Amostras antigas, ausentes ou inválidas não são leituras atuais."
        responded && recognizedValues > 0 -> "O Core enviou valores, mas o formato legado não comprova origem, qualidade e horário de cada sinal. Os instrumentos permanecem sem leitura verificada."
        responded -> "JSON recebido sem campos numéricos compatíveis. É necessário conferir o contrato do Core."
        else -> "Nenhuma leitura recebida. Confira o Wi-Fi do Core e teste novamente."
    }
}
