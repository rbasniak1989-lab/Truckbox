from pathlib import Path

p = Path('multimedia/src/main/java/br/com/truckbox/multimedia/MainActivity.kt')
s = p.read_text(encoding='utf-8')


def replace_once(old: str, new: str, label: str):
    global s
    if old not in s:
        raise SystemExit(f'patch failed: {label}')
    s = s.replace(old, new, 1)

replace_once(
    'Text("Multimídia • v0.2", color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 12.sp)',
    'Text("Multimídia • v0.3", color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 12.sp)',
    'version',
)

replace_once(
    '    val absAmberWarning: Boolean? = null,\n    val combinationWeightKg: Double? = null,',
    '    val absAmberWarning: Boolean? = null,\n'
    '    val fanPgnSeen: Boolean? = null,\n'
    '    val fanEstimatedPct: Double? = null,\n'
    '    val fanDriveStateRaw: Int? = null,\n'
    '    val fanSpeedRpm: Double? = null,\n'
    '    val fd1RawHex: String? = null,\n'
    '    val combinationWeightKg: Double? = null,',
    'LiveState fan fields',
)

replace_once(
    'Text("Todos os sinais mapeados pelo Core V0.6.2. Quando a ECU publica FF/indisponível, mostramos —.", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)',
    'Text("Todos os sinais mapeados pelo Core V0.6.3. Ventoinha/FD1 permanece EXPERIMENTAL até validação no caminhão. FF/indisponível aparece como —.", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)',
    'sensor subtitle',
)

replace_once(
    '            Triple("Torque freio motor", s.retarderTorquePct, "%"),\n            Triple("Pedal freio", s.brakePedalPct, "%"),',
    '            Triple("Torque freio motor", s.retarderTorquePct, "%"),\n'
    '            Triple("Pedal freio", s.brakePedalPct, "%"),\n'
    '            Triple("Ventoinha estimada EXP", s.fanEstimatedPct, "%"),\n'
    '            Triple("Ventoinha RPM EXP", s.fanSpeedRpm, "rpm"),',
    'fan sensor cards',
)

replace_once(
    '                StateLine("Aviso ABS/EBS", s.absAmberWarning, when (s.absAmberWarning) { true -> "ATENÇÃO"; false -> "NORMAL"; null -> "?" })\n            }\n        }',
    '                StateLine("Aviso ABS/EBS", s.absAmberWarning, when (s.absAmberWarning) { true -> "ATENÇÃO"; false -> "NORMAL"; null -> "?" })\n'
    '                HorizontalDivider()\n'
    '                Text("Ventoinha • FD1 • EXPERIMENTAL", fontWeight = FontWeight.Bold, fontSize = 13.sp)\n'
    '                Text("PGN 65213 vista: ${when (s.fanPgnSeen) { true -> "SIM"; false -> "NÃO"; null -> "?" }}", fontSize = 12.sp)\n'
    '                Text("Estado: ${fanDriveStateLabel(s.fanDriveStateRaw)}", fontSize = 12.sp)\n'
    '                Text("FD1 RAW: ${s.fd1RawHex ?: "—"}", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)\n'
    '            }\n        }',
    'fan state block',
)

replace_once(
    '            absAmberWarning = j.boolean("absAmberWarning", "abs_amber_warning"),\n            combinationWeightKg = j.number("combinationWeightKg", "combination_weight_kg", "total_weight_can_kg"),',
    '            absAmberWarning = j.boolean("absAmberWarning", "abs_amber_warning"),\n'
    '            fanPgnSeen = j.boolean("fanPgnSeen", "fan_pgn_seen"),\n'
    '            fanEstimatedPct = j.number("fanEstimatedPct", "fan_estimated_pct"),\n'
    '            fanDriveStateRaw = j.integer("fanDriveStateRaw", "fan_drive_state_raw"),\n'
    '            fanSpeedRpm = j.number("fanSpeedRpm", "fan_speed_rpm"),\n'
    '            fd1RawHex = j.text("fd1RawHex", "fd1_raw_hex"),\n'
    '            combinationWeightKg = j.number("combinationWeightKg", "combination_weight_kg", "total_weight_can_kg"),',
    'parser fan fields',
)

replace_once(
    'private fun LiveState.effectiveAverage(): Double? = tripAverageKml ?: if (tripDistanceKm != null && tripFuelLiters != null && tripFuelLiters > 0.01) tripDistanceKm / tripFuelLiters else null',
    '''private fun fanDriveStateLabel(raw: Int?): String = when (raw) {
    null -> "—"
    0 -> "0 • desligada"
    1 -> "1 • sistema do motor"
    2 -> "2 • temperatura do ar"
    3 -> "3 • temperatura do óleo motor"
    4 -> "4 • temperatura do arrefecimento"
    5 -> "5 • temperatura do câmbio"
    6 -> "6 • temperatura hidráulica"
    7 -> "7 • operação padrão/proteção"
    8 -> "8 • reversão"
    9 -> "9 • controle manual"
    10 -> "10 • retarder da transmissão"
    11 -> "11 • ar-condicionado"
    12 -> "12 • temporizador"
    13 -> "13 • freio motor"
    14 -> "14 • outro"
    else -> "$raw • não definido"
}

private fun LiveState.effectiveAverage(): Double? = tripAverageKml ?: if (tripDistanceKm != null && tripFuelLiters != null && tripFuelLiters > 0.01) tripDistanceKm / tripFuelLiters else null''',
    'fan state label helper',
)

p.write_text(s, encoding='utf-8')
print('Multimedia fan patch applied')
