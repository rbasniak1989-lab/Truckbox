#!/usr/bin/env python3
"""Static consistency checks on the 2026-11-06 TruckBox J1939 inventory."""
import csv
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIGNALS = ROOT / "INVENTARIO_J1939_76_SINAIS.csv"
PGNS = ROOT / "INVENTARIO_J1939_46_PGNS_OBSERVADAS.csv"

with SIGNALS.open(encoding="utf-8-sig", newline="") as fp:
    signals = list(csv.DictReader(fp))
with PGNS.open(encoding="utf-8-sig", newline="") as fp:
    pgn_rows = list(csv.DictReader(fp))

assert len(signals) == 76, len(signals)
assert len(pgn_rows) == 46, len(pgn_rows)
assert len(set(x["PGN"] for x in pgn_rows)) == 46
assert len(set(x["pgn"] for x in signals)) == 29
assert all(x["id"].isdigit() and x["pgn"].startswith("PGN ") and x["sinal"] for x in signals)
assert [int(x["id"]) for x in signals] == list(range(1, 77))
assert Counter(x["status_fonte"] for x in signals) == {
    "Validado": 61, "Parcial": 9, "Pendente": 2,
    "Indisponível": 3, "Sem decodificação": 1,
}
assert Counter(x["situacao"] for x in pgn_rows) == {
    "Há sinal traduzido": 27, "Tráfego sem sinal útil": 9,
    "Alias/artefato": 8, "Sem leitura útil": 2,
}
print("OK: J1939 inventory 76 signal rows / 29 PGNs; 46 observed PGNs; confidence tags")
