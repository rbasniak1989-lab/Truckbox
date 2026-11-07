#!/usr/bin/env python3
"""C/Python equivalence on vectors constructed from Master1.1, not full Logs Mae replay."""
from pathlib import Path
import importlib.util
import math
import struct
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[3]
source = root / 'firmware/espidf/components/tb_core'
refpath = root / 'firmware/j1939/references/truckbox_j1939_parser_v1.py'
spec = importlib.util.spec_from_file_location('truckbox_reference', refpath)
ref = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = ref
spec.loader.exec_module(ref)
sys.dont_write_bytecode = True
checks = 0
with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / 'decoder'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pedantic',f'-I{source}',str(source/'tbx_j1939.c'),str(source/'tests/j1939_reference_runner.c'),'-lm','-o',str(binary)],check=True)
    def decode(pgn, data, sa=0):
        text = subprocess.check_output([str(binary),str(pgn),str(sa),data.hex()], text=True)
        output = {}
        for line in text.splitlines():
            sid,name,q,has,val,txt,validation,raw = line.split('\t')
            output[int(sid),name] = dict(q=int(q),has=bool(int(has)),value=float(val),text=txt,validation=int(validation),raw=bool(int(raw)))
        parser = ref.TruckBoxJ1939Parser()
        py = {s.name:s.value for s in parser.parse(ref.J1939Frame(1000,pgn,sa,len(data),bytes(data)))}
        return output,py
    def equivalent(pgn, data, mapping, sa=0):
        global checks
        c,py = decode(pgn,bytes(data),sa)
        for key,name in mapping.items():
            observed=c[key]
            expected=py[name]
            assert observed['has'],(pgn,key,observed)
            assert math.isclose(observed['value'],expected,rel_tol=1e-11,abs_tol=1e-10),(pgn,key,observed,expected)
            checks += 1
        return c,py
    equivalent(61443,bytes.fromhex('0500000000000000'),{(6,'value'):'engine.accelerator_pedal_pct'})
    c,py=equivalent(61444,bytes.fromhex('0000AF401F000000'),{(1,'value'):'engine.rpm',(2,'value'):'engine.actual_torque_pct',(3,'value'):'engine.power_kw',(3,'actual_torque_nm'):'engine.actual_torque_nm',(3,'power_cv'):'engine.power_cv'})
    assert math.isclose(c[3,'fuel_rate_estimated_lph']['value'],py['engine.power_kw']*0.25*ref.DEFAULT_FUEL_CALIBRATION_FACTOR)
    equivalent(65265,bytes.fromhex('0000320000320000'),{(14,'value'):'vehicle.speed_kph',(16,'value'):'vehicle.cruise_set_speed_kph'})
    equivalent(65217,struct.pack('<II',200000,123456),{(17,'value'):'vehicle.distance_highres_total_km'})
    equivalent(65263,bytes.fromhex('7DFF7D640080FF7D'),{(33,'value'):'engine.fuel_delivery_pressure_bar',(34,'value'):'engine.oil_pressure_bar',(35,'value'):'engine.oil_level_pct',(36,'value'):'engine.crankcase_pressure_kpa',(37,'value'):'engine.coolant_level_pct'})
    equivalent(65270,bytes.fromhex('006464780A000000'),{(29,'value'):'engine.boost_relative_bar',(30,'value'):'engine.intake_manifold_temp_c',(31,'value'):'engine.air_inlet_pressure_kpa_abs',(32,'value'):'engine.air_cleaner_diff_pressure_kpa'})
    equivalent(65269,bytes.fromhex('0000004025000000'),{(28,'value'):'environment.ambient_temp_c'})
    equivalent(65262,bytes.fromhex('7870602D00000000'),{(24,'value'):'engine.coolant_temp_c',(25,'value'):'engine.oil_temp_c',(26,'value'):'engine.fuel_temp_c'})
    equivalent(65272,bytes.fromhex('00000000202C0000'),{(27,'value'):'transmission.oil_temp_c'})
    equivalent(61442,bytes.fromhex('01A00F1900401F00'),{(40,'value'):'transmission.input_shaft_rpm',(41,'value'):'transmission.output_shaft_rpm',(42,'value'):'transmission.clutch_slip_pct'})
    equivalent(61445,bytes.fromhex('89E8038900000000'),{(44,'value'):'transmission.selected_gear',(45,'value'):'transmission.current_gear',(46,'value'):'transmission.actual_gear_ratio'})
    equivalent(61440,bytes.fromhex('0019000000000000'),{(54,'value'):'retarder.actual_torque_pct'})
    equivalent(65132,bytes.fromhex('45000000401F0032'),{(66,'value'):'tachograph.vehicle_motion_state',(68,'value'):'tachograph.output_shaft_rpm',(69,'value'):'tachograph.vehicle_speed_kph'})
    equivalent(65247,bytes.fromhex('8F00000000000000'),{(8,'value'):'engine.nominal_friction_torque_pct'})
    engine=bytes.fromhex('C012B1F041B38025E0E02EDB4038CEF041FFFF530AE047FF32FA7DE1C05D')
    mapping={(9,'value'):'engine.reference_torque_nm',(11,'value'):'engine.high_idle_rpm',(11,'extended_speed_upper_rpm'):'engine.extended_speed_upper_rpm',(12,'torque_control_min_pct'):'engine.torque_control_min_pct',(12,'torque_control_max_pct'):'engine.torque_control_max_pct',(13,'engine_gain_kp_raw'):'engine.gain_kp_raw',(13,'requested_speed_lower_raw'):'engine.requested_speed_lower_raw',(13,'requested_speed_upper_raw'):'engine.requested_speed_upper_raw'}
    for i in range(1,6):
        for axis in ('rpm','pct'):mapping[10,f'p{i}_{axis}']=f'engine.max_torque_curve.p{i}.{axis}'
    equivalent(65251,engine,mapping)
    rc=bytes.fromhex('0304201C2CE04721802522003C1D8034A20319')
    mapping={(56,'reference_retarder_torque_raw'):'retarder.reference_torque_raw'}
    for i in range(1,6):
        for axis in ('rpm','pct'):mapping[55,f'p{i}_{axis}']=f'retarder.curve.p{i}.{axis}'
    equivalent(65249,rc,mapping)
    time=bytes([11,12,13,10,120,41,112,122])
    mapping={(70,n):f'time.can_{n}' for n in ('seconds','minutes','hours','month','day','year')}
    equivalent(65254,time,mapping)
    equivalent(65258,bytes.fromhex('1F204EFFFFFFFFFF'),{(19,'value'):'vehicle.drive_bogie_estimated_weight_kg',(20,'value'):'vehicle.drive_axle_weight_1f_kg'})
    equivalent(65295,bytes.fromhex('0000C10000280000'),{(21,'value'):'vehicle.combination_weight_estimated_kg',(23,'value'):'transmission.iroll_secondary_active'})
    equivalent(65321,b' AN E+00',{(50,'value'):'transmission.iroll_active',(51,'value'):'transmission.economy_mode'})
    c,py=decode(65321,b' A12E+00');assert c[52,'value']['text']==py['transmission.gear_ascii'];assert c[53,'value']['text']==py['transmission.mode_ascii']
    equivalent(65323,bytes.fromhex('DF61DF37F7000000'),{(57,'value'):'retarder.veb_lever_stage',(58,'value'):'retarder.max_stage_selected',(59,'value'):'vehicle.cruise_active',(60,'value'):'vehicle.cruise_upper_speed_kph',(61,'value'):'vehicle.stationary'})
    c,_=decode(65323,bytes.fromhex('C700CF32F3000000'));assert c[59,'value']['has'] and c[59,'value']['value']==0;assert not c[60,'value']['has']
    c,py=decode(65340,bytes.fromhex('000000000000D100'));assert c[62,'value']['value']==1 and c[63,'value']['value']==0 and py['vehicle.cruise_button_action']=='cruise_plus'
    c,py=decode(65317,bytes.fromhex('44D0FFFFFFFF0000'));assert c[72,'value']['validation']==1 and not c[72,'value']['has'];assert py['lighting.main_state']=='low_beam_probable'
    # Safety refinements deliberately stricter than PythonV1: reserved/error/NA,
    # invalid calendar, unknown state, corrupted/static profile never usable.
    c,_=decode(61444,bytes.fromhex('0000FFFFFFFFFFFF'));assert not c[1,'value']['has'] and not c[2,'value']['has'] and not c[3,'value']['has']
    corrupt=bytearray(engine);corrupt[19]=0x54;c,_=decode(65251,corrupt);assert c[9,'value']['q']==12 and not c[9,'value']['has']
    corrupt=bytearray(engine);corrupt[0]=0xC1;c,_=decode(65251,corrupt);assert c[10,'p1_rpm']['q']==12 and not c[10,'p1_rpm']['has']
    c,_=decode(65254,bytes([0,0,0,2,120,41,255,255]));assert c[70,'value']['q']==5 and not c[70,'value']['text']
    c,py=decode(65343,b' AN E+00',41);assert not c and not py
print(f'J1939 C/PythonV1: {checks} scalar/component equivalence checks + explicit safety differences PASS; constructed Master vectors, no full Logs Mae replay')
