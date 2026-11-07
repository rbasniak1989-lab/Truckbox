#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""TruckBox Parser J1939 V1.

Implementação nova e independente, baseada no Documento Mestre J1939 v1.0
(11/08/2026). Não depende do antigo TruckBox_PGN_Validator_v1.py.

Camadas:
  1) J1939Frame: frame bruto normalizado.
  2) TruckBoxJ1939Parser: PGN -> sinais decodificados.
  3) TruckBoxState: snapshot do último valor de cada sinal.
  4) FuelPowerAccumulator: grandeza derivada de potência/consumo.

Regras importantes:
- PGN + SA + DLC são preservados como origem do sinal.
- Campos não fechados no Documento Mestre não recebem escala inventada.
- PGNs padrão têm preferência sobre agregadores proprietários Volvo.
- 65258 1F/2F são redundantes: nunca somar.
- I-Roll canônico: 65321. VEB: alavanca 65323, torque efetivo 61440.
- Consumo é derivado; PGN 65304 NÃO é fonte canônica nesta versão.
"""
from __future__ import annotations

from dataclasses import dataclass, field, asdict
from enum import Enum
from typing import Any, Dict, Iterable, List, Mapping, Optional, Tuple
import csv
import json
import math
import re

VERSION = "1.0.0"
DEFAULT_REFERENCE_TORQUE_NM = 2643.0
DEFAULT_FUEL_CALIBRATION_FACTOR = 0.9720031611625598  # Log Mãe vazio: 117.1806889 -> 113.9 L
MASTER_ENGINE_CURVE = {
    1: (600.0, 52.0),
    2: (2110.0, 54.0),
    3: (1200.0, 99.0),
    4: (1500.0, 94.0),
    5: (1800.0, 81.0),
}


class Quality(str, Enum):
    VALID = "valid"
    PARTIAL = "partial"
    UNAVAILABLE = "unavailable"
    SUSPECT_ALIAS = "suspect_alias"
    STALE = "stale"


@dataclass(frozen=True)
class Signal:
    name: str
    value: Any
    unit: Optional[str]
    quality: Quality
    pgn: int
    sa: Optional[int]
    field: str
    note: str = ""

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d["quality"] = self.quality.value
        return d


@dataclass(frozen=True)
class AliasInfo:
    observed_pgn: int
    observed_sa: Optional[int]
    probable_original_pgn: Optional[int]
    reason: str


@dataclass(frozen=True)
class J1939Frame:
    timestamp_ms: Optional[float]
    pgn: int
    sa: Optional[int]
    dlc: int
    data: bytes
    can_id: Optional[int] = None

    @staticmethod
    def pgn_from_can_id(can_id: int) -> int:
        pf = (can_id >> 16) & 0xFF
        ps = (can_id >> 8) & 0xFF
        dp = (can_id >> 24) & 0x01
        return (dp << 16) | (pf << 8) if pf < 240 else (dp << 16) | (pf << 8) | ps

    @staticmethod
    def sa_from_can_id(can_id: int) -> int:
        return can_id & 0xFF

    @classmethod
    def from_csv_row(cls, row: Mapping[str, str]) -> "J1939Frame":
        can_id = _parse_int(row.get("CanIdHex") or row.get("CAN ID") or row.get("CANID"), prefer_hex=True)
        pgn = _parse_int(row.get("PGN"))
        if pgn is None and can_id is not None:
            pgn = cls.pgn_from_can_id(can_id)
        if pgn is None:
            raise ValueError("Frame sem PGN/CAN ID")
        sa = _parse_int(row.get("SA") or row.get("SourceAddress") or row.get("Source Address"))
        if sa is None and can_id is not None:
            sa = cls.sa_from_can_id(can_id)
        dlc = _parse_int(row.get("DLC"))
        data = _payload_from_row(row)
        if dlc is None:
            dlc = len(data)
        ts = _parse_float(row.get("SessionMs") or row.get("Timestamp") or row.get("Time") or row.get("Tempo"))
        return cls(timestamp_ms=ts, pgn=int(pgn), sa=sa, dlc=int(dlc), data=data, can_id=can_id)


@dataclass
class TruckBoxState:
    # signals = último valor operacional aceito; observed_signals = último frame, inclusive N/A/suspeito.
    signals: Dict[str, Signal] = field(default_factory=dict)
    observed_signals: Dict[str, Signal] = field(default_factory=dict)
    suspect_signals: Dict[str, Signal] = field(default_factory=dict)
    reference_torque_nm: float = DEFAULT_REFERENCE_TORQUE_NM
    retarder_curve: List[Tuple[float, float]] = field(default_factory=list)  # rpm, torque_pct
    drive_axle_weight_1f_kg: Optional[float] = None
    drive_axle_weight_2f_kg: Optional[float] = None

    def update(self, decoded: Iterable[Signal]) -> None:
        for s in decoded:
            self.observed_signals[s.name] = s
            if s.quality in (Quality.SUSPECT_ALIAS, Quality.UNAVAILABLE):
                self.suspect_signals[s.name] = s
                continue
            self.signals[s.name] = s
            if s.name == "engine.reference_torque_nm" and isinstance(s.value, (int, float)):
                self.reference_torque_nm = float(s.value)
            elif s.name == "vehicle.drive_axle_weight_1f_kg":
                self.drive_axle_weight_1f_kg = float(s.value)
            elif s.name == "vehicle.drive_axle_weight_2f_kg":
                self.drive_axle_weight_2f_kg = float(s.value)

    def value(self, name: str, default: Any = None) -> Any:
        """Último valor operacional aceito (não sobrescrito por N/A/suspeito)."""
        s = self.signals.get(name)
        return s.value if s else default

    def latest(self, name: str) -> Optional[Signal]:
        """Última observação recebida, com quality preservada."""
        return self.observed_signals.get(name)

    @property
    def drive_bogie_estimated_weight_kg(self) -> Optional[float]:
        vals = [x for x in (self.drive_axle_weight_1f_kg, self.drive_axle_weight_2f_kg) if x is not None]
        return sum(vals) / len(vals) if vals else None

    def max_torque_at_rpm_nm(self, rpm: float) -> Optional[float]:
        if not self.retarder_curve:  # engine curve is stored separately by parser signals; see helper below
            pass
        curve = []
        for i in range(1, 6):
            r = self.value(f"engine.max_torque_curve.p{i}.rpm")
            p = self.value(f"engine.max_torque_curve.p{i}.pct")
            if isinstance(r, (int, float)) and isinstance(p, (int, float)):
                curve.append((float(r), float(p)))
        if not curve:
            return None
        curve.sort()
        pct = _linear_interpolate(curve, rpm)
        return self.reference_torque_nm * pct / 100.0


class TruckBoxJ1939Parser:
    """Decodificador canônico TruckBox V1."""

    USEFUL_PGNS = {
        61440, 61441, 61442, 61443, 61444, 61445,
        65132,
        65217, 65247, 65249, 65251, 65254, 65258, 65262, 65263,
        65265, 65269, 65270, 65272, 65282, 65286, 65292, 65295,
        65317, 65321, 65323, 65340,
    }

    # Regras específicas das assinaturas observadas nos Logs Mãe.
    # Não significam que a PGN oficial seja inválida globalmente.
    _ALIAS_BY_PGN_SA = {
        **{(61456, sa): 61440 + sa for sa in range(6)},
        (65150, 108): 65132,
        (65246, 223): 65247,
        (65246, 193): 65217,
        (65214, 191): 65215,
        (65118, 86): 65110,
        (65311, 2): 65282,
        (65311, 6): 65286,
        (65311, 12): 65292,
        (65311, 15): 65295,
        (65343, 37): 65317,
        (65343, 40): 65320,
        (65343, 41): 65321,
        (65343, 43): 65323,
        (65343, 60): 65340,
        (65278, 230): 65254,
        (65278, 234): 65258,
        (65278, 238): 65262,
        (65278, 239): 65263,
        (65278, 241): 65265,
        (65278, 245): 65269,
        (65278, 246): 65270,
        (65278, 248): 65272,
    }

    def __init__(self) -> None:
        self.state = TruckBoxState()

    def classify_alias(self, frame: J1939Frame) -> Optional[AliasInfo]:
        original = self._ALIAS_BY_PGN_SA.get((frame.pgn, frame.sa))
        if original is None:
            return None
        return AliasInfo(frame.pgn, frame.sa, original,
                         "Assinatura PGN+SA observada como alias/artefato nos Logs Mãe; não decodificar como PGN oficial.")

    def parse(self, frame: J1939Frame) -> List[Signal]:
        if self.classify_alias(frame) is not None:
            return []
        fn = getattr(self, f"_decode_{frame.pgn}", None)
        if fn is None:
            return []
        out = fn(frame)
        self.state.update(out)
        return out

    @staticmethod
    def _sig(frame: J1939Frame, name: str, value: Any, unit: Optional[str], field: str,
             quality: Quality = Quality.VALID, note: str = "") -> Signal:
        return Signal(name, value, unit, quality, frame.pgn, frame.sa, field, note)

    def _decode_61443(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 2: return []
        return [self._sig(f, "engine.accelerator_pedal_pct", f.data[1] * 0.4, "%", "B1")]

    def _decode_61444(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 5: return []
        tq_pct = f.data[2] - 125
        rpm = _le16(f.data, 3) * 0.125
        tq_nm = self.state.reference_torque_nm * max(tq_pct, 0.0) / 100.0
        power_kw = tq_nm * rpm / 9550.0
        out = [
            self._sig(f, "engine.actual_torque_pct", tq_pct, "%", "B2"),
            self._sig(f, "engine.rpm", rpm, "rpm", "B3:B4 LE"),
            self._sig(f, "engine.actual_torque_nm", tq_nm, "Nm", "derived(B2,refTorque)"),
            self._sig(f, "engine.power_kw", power_kw, "kW", "derived(torqueNm,rpm)"),
            self._sig(f, "engine.power_cv", power_kw * 1.35962, "cv", "derived(powerKw)"),
        ]
        max_tq = self.state.max_torque_at_rpm_nm(rpm)
        if max_tq and max_tq > 0:
            out.append(self._sig(f, "engine.relative_load_available", tq_nm / max_tq, "ratio", "derived(curve)"))
        return out

    def _decode_65265(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 6: return []
        speed = _le16(f.data, 1) / 256.0
        out = [
            self._sig(f, "vehicle.speed_kph", speed, "km/h", "B1:B2 LE"),
            self._sig(f, "vehicle.parking_state_raw", f.data[0], None, "B0", Quality.PARTIAL,
                      "Estado funcional validado; bit nominal não fixado no Documento Mestre."),
        ]
        if f.data[5] != 0xFF:
            out.append(self._sig(f, "vehicle.cruise_set_speed_kph", float(f.data[5]), "km/h", "B5"))
        return out

    def _decode_65217(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 8: return []
        # Layout J1939 VDHR: dois contadores LE32 com resolução 5 m/count.
        # O primeiro é exposto como distância CAN principal; o segundo é mantido separadamente.
        total_km = _le32(f.data, 0) * 0.005
        trip_km = _le32(f.data, 4) * 0.005
        return [
            self._sig(f, "vehicle.distance_highres_total_km", total_km, "km", "B0:B3 LE32"),
            self._sig(f, "vehicle.distance_highres_trip_km", trip_km, "km", "B4:B7 LE32"),
        ]

    def _decode_65263(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 8: return []
        out = []
        if f.data[0] != 0xFF:
            out.append(self._sig(f, "engine.fuel_delivery_pressure_bar", f.data[0] * 0.04, "bar", "B0"))
        if f.data[1] == 0xFF:
            out.append(self._sig(f, "engine.blowby", None, None, "B1", Quality.UNAVAILABLE))
        if f.data[2] != 0xFF:
            out.append(self._sig(f, "engine.oil_level_pct", f.data[2] * 0.4, "%", "B2"))
        if f.data[3] != 0xFF:
            out.append(self._sig(f, "engine.oil_pressure_bar", f.data[3] * 0.04, "bar", "B3"))
        raw_cc = _le16(f.data, 4)
        if raw_cc != 0xFFFF:
            out.append(self._sig(f, "engine.crankcase_pressure_kpa", raw_cc / 128.0 - 250.0, "kPa", "B4:B5 LE"))
        if f.data[6] == 0xFF:
            out.append(self._sig(f, "engine.coolant_pressure", None, None, "B6", Quality.UNAVAILABLE))
        if f.data[7] != 0xFF:
            out.append(self._sig(f, "engine.coolant_level_pct", f.data[7] * 0.4, "%", "B7", Quality.PARTIAL,
                                 "Campo classificado como provável/forte no Documento Mestre."))
        return out

    def _decode_65270(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 5: return []
        out = []
        if f.data[1] != 0xFF:
            out += [
                self._sig(f, "engine.intake_manifold_pressure_kpa", f.data[1] * 2.0, "kPa", "B1"),
                self._sig(f, "engine.boost_relative_bar", f.data[1] * 0.02, "bar", "B1"),
            ]
        if f.data[2] != 0xFF:
            out.append(self._sig(f, "engine.intake_manifold_temp_c", f.data[2] - 40.0, "°C", "B2"))
        if f.data[3] != 0xFF:
            out.append(self._sig(f, "engine.air_inlet_pressure_kpa_abs", f.data[3] * 2.0, "kPa abs", "B3"))
        if f.data[4] != 0xFF:
            out.append(self._sig(f, "engine.air_cleaner_diff_pressure_kpa", f.data[4] * 0.05, "kPa", "B4"))
        return out

    def _decode_65269(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 5: return []
        raw = _le16(f.data, 3)
        if raw == 0xFFFF: return []
        return [self._sig(f, "environment.ambient_temp_c", raw * 0.03125 - 273.0, "°C", "B3:B4 LE")]

    def _decode_65262(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 4: return []
        out = []
        if f.data[0] != 0xFF:
            out.append(self._sig(f, "engine.coolant_temp_c", f.data[0] - 40.0, "°C", "B0"))
        if f.data[1] != 0xFF:
            out.append(self._sig(f, "engine.fuel_temp_c", f.data[1] - 40.0, "°C", "B1", Quality.PARTIAL,
                                 "No FH observado espelha coolant/fallback; não tratar como sensor independente confiável."))
        raw = _le16(f.data, 2)
        if raw != 0xFFFF:
            out.append(self._sig(f, "engine.oil_temp_c", raw * 0.03125 - 273.0, "°C", "B2:B3 LE"))
        return out

    def _decode_65272(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 6: return []
        raw = _le16(f.data, 4)
        if raw == 0xFFFF: return []
        v = raw * 0.03125 - 273.0
        q = Quality.VALID if -40.0 <= v <= 200.0 else Quality.SUSPECT_ALIAS
        return [self._sig(f, "transmission.oil_temp_c", v, "°C", "B4:B5 LE", q,
                          "Valor fisicamente impossível; tratar como corrupção isolada." if q != Quality.VALID else "")]

    def _decode_61442(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 7: return []
        out = [self._sig(f, "transmission.etc1_state_raw", f.data[0], None, "B0", Quality.PARTIAL)]
        raw_out = _le16(f.data, 1)
        if raw_out != 0xFFFF:
            v = raw_out * 0.125
            q = Quality.VALID if 0.0 <= v <= 3000.0 else Quality.SUSPECT_ALIAS
            out.append(self._sig(f, "transmission.output_shaft_rpm", v, "rpm", "B1:B2 LE", q,
                                 "Fora da faixa mecânica esperada => provável corrupção isolada do logger." if q != Quality.VALID else ""))
        if f.data[3] != 0xFF:
            out.append(self._sig(f, "transmission.clutch_slip_pct", f.data[3] * 0.4, "%", "B3"))
        raw_in = _le16(f.data, 5)
        if raw_in != 0xFFFF:
            v = raw_in * 0.125
            q = Quality.VALID if 0.0 <= v <= 3000.0 else Quality.SUSPECT_ALIAS
            out.append(self._sig(f, "transmission.input_shaft_rpm", v, "rpm", "B5:B6 LE", q,
                                 "Fora da faixa mecânica esperada => provável corrupção isolada do logger." if q != Quality.VALID else ""))
        return out

    def _decode_61445(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 4: return []
        out = []
        if f.data[0] != 0xFF:
            out.append(self._sig(f, "transmission.selected_gear", f.data[0] - 125, None, "B0"))
        ratio = _le16(f.data, 1)
        if ratio != 0xFFFF:
            out.append(self._sig(f, "transmission.actual_gear_ratio", ratio * 0.001, "ratio", "B1:B2 LE"))
        if f.data[3] != 0xFF:
            out.append(self._sig(f, "transmission.current_gear", f.data[3] - 125, None, "B3"))
        return out

    def _decode_61440(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 2 or f.data[1] == 0xFF: return []
        return [self._sig(f, "retarder.actual_torque_pct", f.data[1] - 125, "%", "B1")]

    def _decode_61441(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 6: return []
        return [
            self._sig(f, "brakes.ebc1_b0_raw", f.data[0], None, "B0", Quality.PARTIAL,
                      "ABS Active foi validado funcionalmente, mas o Documento Mestre não fixa a máscara do bit."),
            self._sig(f, "brakes.ebc1_b5_raw", f.data[5], None, "B5", Quality.PARTIAL,
                      "Amber ABS/EBS warning validado funcionalmente; máscara nominal fica pendente."),
        ]

    def _decode_65132(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 8: return []
        motion = (f.data[0] >> 6) & 0x03
        driver_state = f.data[0] & 0x07
        out = [
            self._sig(f, "tachograph.vehicle_motion_state", motion, None, "B0 bits6-7"),
            self._sig(f, "tachograph.driver1_working_state_raw", driver_state, None, "B0 bits0-2", Quality.PARTIAL),
        ]
        raw_os = _le16(f.data, 4)
        if raw_os != 0xFFFF:
            out.append(self._sig(f, "tachograph.output_shaft_rpm", raw_os * 0.125, "rpm", "B4:B5 LE"))
        raw_sp = _le16(f.data, 6)
        if raw_sp != 0xFFFF:
            out.append(self._sig(f, "tachograph.vehicle_speed_kph", raw_sp / 256.0, "km/h", "B6:B7 LE"))
        return out

    def _decode_65247(self, f: J1939Frame) -> List[Signal]:
        if not f.data or f.data[0] == 0xFF: return []
        return [self._sig(f, "engine.nominal_friction_torque_pct", f.data[0] - 125, "%", "B0")]

    def _decode_65249(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 18: return []
        out = [
            self._sig(f, "retarder.config.type_location_raw", f.data[0], None, "B0", Quality.PARTIAL),
            self._sig(f, "retarder.config.control_method_raw", f.data[1], None, "B1", Quality.PARTIAL),
        ]
        # P1..P4 são blocos rpm LE16 + torque. O P5 é especial no frame Volvo observado:
        # B14:B15 = rpm P5; B16:B17 = Reference Retarder Torque; B18 = torque P5.
        for idx, off in enumerate((2, 5, 8, 11), 1):
            rpm_raw = _le16(f.data, off)
            tq_raw = f.data[off + 2]
            if rpm_raw != 0xFFFF:
                out.append(self._sig(f, f"retarder.curve.p{idx}.rpm", rpm_raw * 0.125, "rpm", f"B{off}:B{off+1} LE"))
            if tq_raw != 0xFF:
                out.append(self._sig(f, f"retarder.curve.p{idx}.pct", tq_raw - 125, "%", f"B{off+2}"))
        rpm5_raw = _le16(f.data, 14)
        if rpm5_raw != 0xFFFF:
            out.append(self._sig(f, "retarder.curve.p5.rpm", rpm5_raw * 0.125, "rpm", "B14:B15 LE"))
        ref_raw = _le16(f.data, 16)
        if ref_raw != 0xFFFF:
            out.append(self._sig(f, "retarder.reference_torque_raw", ref_raw, None, "B16:B17 LE", Quality.PARTIAL,
                                 "Escala física exata não resolvida."))
        if len(f.data) >= 19 and f.data[18] != 0xFF:
            out.append(self._sig(f, "retarder.curve.p5.pct", f.data[18] - 125, "%", "B18"))
        return out

    def _decode_65251(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 30: return []
        out = []
        for idx, off in enumerate((0, 3, 6, 9, 12), 1):
            rpm_raw = _le16(f.data, off)
            tq_raw = f.data[off + 2]
            exp_rpm, exp_pct = MASTER_ENGINE_CURVE[idx]
            if rpm_raw != 0xFFFF:
                rpm = rpm_raw * 0.125
                q = Quality.VALID if rpm == exp_rpm else Quality.SUSPECT_ALIAS
                out.append(self._sig(f, f"engine.max_torque_curve.p{idx}.rpm", rpm, "rpm", f"B{off}:B{off+1} LE", q,
                                     "Configuração estática divergente do perfil Mestre; provável corrupção do logger." if q != Quality.VALID else ""))
            if tq_raw != 0xFF:
                pct = tq_raw - 125
                q = Quality.VALID if pct == exp_pct else Quality.SUSPECT_ALIAS
                out.append(self._sig(f, f"engine.max_torque_curve.p{idx}.pct", pct, "%", f"B{off+2}", q,
                                     "Configuração estática divergente do valor modal validado." if q != Quality.VALID else ""))
        hi = _le16(f.data, 15)
        if hi != 0xFFFF:
            out.append(self._sig(f, "engine.high_idle_rpm", hi * 0.125, "rpm", "B15:B16 LE"))
        ref = _le16(f.data, 19)
        if ref != 0xFFFF:
            q = Quality.VALID if ref == int(DEFAULT_REFERENCE_TORQUE_NM) else Quality.SUSPECT_ALIAS
            out.append(self._sig(f, "engine.reference_torque_nm", float(ref), "Nm", "B19:B20 LE", q,
                                 "Para o perfil Mestre, Reference Torque canônico = 2643 Nm." if q != Quality.VALID else ""))
        kp = _le16(f.data, 21)
        if kp != 0xFFFF:
            out.append(self._sig(f, "engine.gain_kp_raw", kp, None, "B21:B22 LE", Quality.PARTIAL))
        if f.data[24] != 0xFF:
            out.append(self._sig(f, "engine.requested_speed_lower_raw", f.data[24], None, "B24", Quality.PARTIAL))
        if f.data[25] != 0xFF:
            out.append(self._sig(f, "engine.requested_speed_upper_raw", f.data[25], None, "B25", Quality.PARTIAL))
        if f.data[26] != 0xFF:
            out.append(self._sig(f, "engine.torque_control_min_pct", f.data[26] - 125, "%", "B26"))
        if f.data[27] != 0xFF:
            out.append(self._sig(f, "engine.torque_control_max_pct", f.data[27] - 125, "%", "B27"))
        ext = _le16(f.data, 28)
        if ext != 0xFFFF:
            out.append(self._sig(f, "engine.extended_speed_upper_rpm", ext * 0.125, "rpm", "B28:B29 LE"))
        return out

    def _decode_65254(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 8: return []
        out = []
        vals = f.data
        if vals[0] != 0xFF: out.append(self._sig(f, "time.can_seconds", vals[0] * 0.25, "s", "B0"))
        if vals[1] != 0xFF: out.append(self._sig(f, "time.can_minutes", vals[1], "min", "B1"))
        if vals[2] != 0xFF: out.append(self._sig(f, "time.can_hours", vals[2], "h", "B2"))
        if vals[3] != 0xFF: out.append(self._sig(f, "time.can_month", vals[3], None, "B3"))
        if vals[4] != 0xFF: out.append(self._sig(f, "time.can_day", math.ceil(vals[4] / 4.0), None, "B4"))
        if vals[5] != 0xFF: out.append(self._sig(f, "time.can_year", vals[5] + 1985, None, "B5"))
        if vals[6] != 0xFF: out.append(self._sig(f, "time.local_minute_offset_raw", vals[6], None, "B6", Quality.PARTIAL))
        if vals[7] != 0xFF: out.append(self._sig(f, "time.local_hour_offset_raw", vals[7], None, "B7", Quality.PARTIAL))
        return out

    def _decode_65258(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 3: return []
        loc = f.data[0]
        raw = _le16(f.data, 1)
        if raw == 0xFFFF: return []
        kg = raw * 0.5
        out = [self._sig(f, "vehicle.axle_location_raw", loc, None, "B0")]
        if loc == 0x1F:
            out.append(self._sig(f, "vehicle.drive_axle_weight_1f_kg", kg, "kg", "B1:B2 LE"))
        elif loc == 0x2F:
            out.append(self._sig(f, "vehicle.drive_axle_weight_2f_kg", kg, "kg", "B1:B2 LE"))
        else:
            out.append(self._sig(f, "vehicle.axle_weight_kg", kg, "kg", "B1:B2 LE", Quality.PARTIAL,
                                 "Axle Location diferente de 0x1F/0x2F; manter canal separado."))
        # Exposição calculada sem somar canais.
        vals = [x for x in (self.state.drive_axle_weight_1f_kg, self.state.drive_axle_weight_2f_kg) if x is not None]
        vals.append(kg) if loc in (0x1F, 0x2F) else None
        if vals:
            # Deduplica o valor atual se o mesmo canal já existia usando apenas snapshot pós-update aproximado.
            if loc == 0x1F and self.state.drive_axle_weight_2f_kg is not None:
                bogie = (kg + self.state.drive_axle_weight_2f_kg) / 2.0
            elif loc == 0x2F and self.state.drive_axle_weight_1f_kg is not None:
                bogie = (kg + self.state.drive_axle_weight_1f_kg) / 2.0
            else:
                bogie = kg
            out.append(self._sig(f, "vehicle.drive_bogie_estimated_weight_kg", bogie, "kg", "derived(1F,2F)",
                                 note="Média/redundância; nunca soma."))
        return out

    def _decode_65295(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 6: return []
        out = [self._sig(f, "vehicle.proprietary_speed_state_raw", f.data[0], None, "B0", Quality.PARTIAL)]
        if f.data[2] in (0xC0, 0xC1):
            out.append(self._sig(f, "transmission.iroll_secondary_active", f.data[2] == 0xC1, None, "B2",
                                 note="Sinal secundário; canônico é PGN65321."))
        if f.data[5] != 0xFF:
            kg = f.data[5] * 1000.0
            q = Quality.VALID if 10000.0 <= kg <= 100000.0 else Quality.SUSPECT_ALIAS
            out.append(self._sig(f, "vehicle.combination_weight_estimated_kg", kg, "kg", "B5", q,
                                 "Aproximadamente 1 t/count; fora de 10-100 t é tratado como corrupção no perfil atual."))
        return out

    def _decode_65321(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 8: return []
        text = f.data[:8].decode("ascii", errors="replace")
        return [
            self._sig(f, "transmission.status_ascii", text, None, "B0:B7 ASCII"),
            self._sig(f, "transmission.iroll_active", text == " AN E+00", None, "B0:B7 ASCII",
                      note="Assinatura canônica exata observada para I-Roll ativo."),
            self._sig(f, "transmission.mode_ascii", chr(f.data[1]) if 32 <= f.data[1] < 127 else None, None, "B1"),
            self._sig(f, "transmission.gear_ascii", f.data[2:4].decode("ascii", errors="replace"), None, "B2:B3"),
            self._sig(f, "transmission.economy_mode", f.data[4] == ord("E"), None, "B4"),
        ]

    def _decode_65323(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 5: return []
        cruise_active = f.data[0] == 0xDF
        cruise_transition = f.data[0] == 0xD7
        cruise_off = f.data[0] == 0xC7
        veb = (f.data[1] >> 5) & 0x07
        b3_valid = (f.data[1] & 0x01) != 0
        max_sel = f.data[2] == 0xDF
        stationary = (f.data[4] & 0x04) != 0
        out = [
            self._sig(f, "vehicle.cruise_active", cruise_active, None, "B0"),
            self._sig(f, "vehicle.cruise_transition", cruise_transition, None, "B0"),
            self._sig(f, "vehicle.cruise_off", cruise_off, None, "B0"),
            self._sig(f, "retarder.veb_lever_stage", veb, None, "B1 bits5-7"),
            self._sig(f, "vehicle.cruise_upper_valid", b3_valid, None, "B1 bit0"),
            self._sig(f, "retarder.max_stage_selected", max_sel, None, "B2"),
            self._sig(f, "vehicle.stationary", stationary, None, "B4 bit2"),
        ]
        if b3_valid and f.data[3] != 0xFF:
            out.append(self._sig(f, "vehicle.cruise_upper_speed_kph", float(f.data[3]), "km/h", "B3",
                                 note="Validado como cruise set + 5 km/h; nome Volvo exato pendente."))
        return out

    def _decode_65340(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 7: return []
        mapping = {0xC1: "normal", 0xC5: "cruise_minus", 0xD1: "cruise_plus"}
        val = mapping.get(f.data[6], "other")
        q = Quality.VALID if f.data[6] in mapping else Quality.PARTIAL
        return [self._sig(f, "vehicle.cruise_button_action", val, None, "B6", q)]

    def _decode_65317(self, f: J1939Frame) -> List[Signal]:
        if len(f.data) < 2: return []
        light_map = {0xC0: "main_inactive_or_init", 0xD0: "low_beam_probable", 0xD2: "high_beam_probable", 0xD1: "rare_transient"}
        return [
            self._sig(f, "lighting.command_b0_raw", f.data[0], None, "B0", Quality.PARTIAL),
            self._sig(f, "lighting.main_state", light_map.get(f.data[1], f"raw_0x{f.data[1]:02X}"), None, "B1", Quality.PARTIAL,
                      "PGN parcialmente decodificada; nominalização exige teste dirigido."),
        ]

    # Proprietárias funcionais cujo byte exato não foi registrado no Documento Mestre.
    # Mantemos payload bruto sem inventar posição/escala.
    def _decode_65282(self, f: J1939Frame) -> List[Signal]:
        return [self._sig(f, "volvo.65282_payload_hex", f.data.hex(" ").upper(), None, "payload", Quality.PARTIAL,
                          "Agregador funcional; preferir PGNs padrão.")]

    def _decode_65286(self, f: J1939Frame) -> List[Signal]:
        return [self._sig(f, "volvo.65286_payload_hex", f.data.hex(" ").upper(), None, "payload", Quality.PARTIAL,
                          "Seletor I-Shift validado funcionalmente; posição exata não transcrita no Documento Mestre.")]

    def _decode_65292(self, f: J1939Frame) -> List[Signal]:
        return [self._sig(f, "volvo.65292_payload_hex", f.data.hex(" ").upper(), None, "payload", Quality.PARTIAL,
                          "Indicador top gear validado funcionalmente; posição exata não transcrita no Documento Mestre.")]


@dataclass
class FuelPowerAccumulator:
    """Integra o modelo de consumo derivado do Documento Mestre.

    L/h = power_kW * 0.25 * calibration_factor
    C0 padrão V1 = 0.9720031611625598, recalibrado no Log Mãe vazio (113,9 L).
    power_kW = max(torque_pct,0)/100 * reference_torque_nm * rpm / 9550

    Só integra em frames EEC1 (PGN 61444) usando SessionMs.
    """
    reference_torque_nm: float = DEFAULT_REFERENCE_TORQUE_NM
    calibration_factor: float = DEFAULT_FUEL_CALIBRATION_FACTOR
    liters: float = 0.0
    last_timestamp_ms: Optional[float] = None
    last_lph: Optional[float] = None
    samples: int = 0
    skipped_bad_dt: int = 0

    def ingest(self, frame: J1939Frame) -> Optional[float]:
        if frame.pgn != 61444 or len(frame.data) < 5 or frame.timestamp_ms is None:
            return None
        tq_pct = frame.data[2] - 125
        rpm = _le16(frame.data, 3) * 0.125
        torque_nm = self.reference_torque_nm * max(tq_pct, 0.0) / 100.0
        power_kw = torque_nm * rpm / 9550.0
        lph = power_kw * 0.25 * self.calibration_factor
        if self.last_timestamp_ms is not None:
            dt_s = (frame.timestamp_ms - self.last_timestamp_ms) / 1000.0
            if 0.0 < dt_s <= 1.0:
                # Retângulo pela taxa atual, coerente com a baseline histórica sum(L/h * dt).
                self.liters += lph * dt_s / 3600.0
            else:
                self.skipped_bad_dt += 1
        self.last_timestamp_ms = frame.timestamp_ms
        self.last_lph = lph
        self.samples += 1
        return lph


# ------------------------- utilidades -------------------------

def _parse_int(v: Any, prefer_hex: bool = False) -> Optional[int]:
    if v is None: return None
    if isinstance(v, int): return v
    s = str(v).strip()
    if not s: return None
    s = s.replace("0x", "").replace("0X", "")
    try:
        if prefer_hex:
            return int(s, 16)
        if re.fullmatch(r"[0-9]+", s):
            return int(s, 10)
        if re.fullmatch(r"[0-9A-Fa-f]+", s):
            return int(s, 16)
    except ValueError:
        return None
    return None


def _parse_float(v: Any) -> Optional[float]:
    try:
        if v is None or str(v).strip() == "": return None
        return float(v)
    except (ValueError, TypeError):
        return None


def _payload_from_row(row: Mapping[str, str]) -> bytes:
    data_hex = row.get("DataHex") or row.get("Data") or row.get("Payload")
    if data_hex:
        parts = re.findall(r"\b[0-9A-Fa-f]{2}\b", str(data_hex))
        if parts:
            return bytes(int(x, 16) for x in parts)
    vals = []
    for i in range(64):
        k = f"B{i}Hex"
        if k not in row:
            k = f"B{i}"
        if k not in row:
            break
        x = _parse_int(row.get(k), prefer_hex=k.endswith("Hex"))
        if x is None:
            break
        vals.append(x & 0xFF)
    return bytes(vals)


def _le16(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def _le32(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24)


def _linear_interpolate(points: List[Tuple[float, float]], x: float) -> float:
    if x <= points[0][0]: return points[0][1]
    if x >= points[-1][0]: return points[-1][1]
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        if x0 <= x <= x1:
            if x1 == x0: return y0
            t = (x - x0) / (x1 - x0)
            return y0 + t * (y1 - y0)
    return points[-1][1]


def decode_csv_file(path: str, output_jsonl: Optional[str] = None) -> Dict[str, Any]:
    parser = TruckBoxJ1939Parser()
    frames = signals = 0
    outfh = open(output_jsonl, "w", encoding="utf-8") if output_jsonl else None
    try:
        with open(path, "r", encoding="utf-8-sig", newline="") as fh:
            reader = csv.DictReader(fh)
            for row in reader:
                frame = J1939Frame.from_csv_row(row)
                frames += 1
                decoded = parser.parse(frame)
                signals += len(decoded)
                if outfh and decoded:
                    outfh.write(json.dumps({"timestamp_ms": frame.timestamp_ms, "pgn": frame.pgn, "sa": frame.sa,
                                            "signals": [s.to_dict() for s in decoded]}, ensure_ascii=False) + "\n")
    finally:
        if outfh: outfh.close()
    return {"frames": frames, "signals": signals, "reference_torque_nm": parser.state.reference_torque_nm,
            "drive_bogie_estimated_weight_kg": parser.state.drive_bogie_estimated_weight_kg}


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="TruckBox Parser J1939 V1")
    ap.add_argument("csv", nargs="?", help="CSV RAW_CAN do TruckBox")
    ap.add_argument("--jsonl", help="Saída opcional dos sinais decodificados")
    ap.add_argument("--version", action="store_true")
    args = ap.parse_args()
    if args.version:
        print(VERSION)
    elif args.csv:
        print(json.dumps(decode_csv_file(args.csv, args.jsonl), ensure_ascii=False, indent=2))
    else:
        ap.print_help()