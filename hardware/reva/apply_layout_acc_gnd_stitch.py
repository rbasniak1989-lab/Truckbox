from pathlib import Path

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# ACC_N routing splits a small F.Cu GND island around Q3/R11/R12/C21.
# Stitch that island to the continuous inner GND plane. The chosen point is
# inside the island, clear of ACC_N/ACC_BASE copper and outside component pads.
via = '  (via (at 86.000 36.000) (size 0.700) (drill 0.350) (layers "F.Cu" "B.Cu") (net "GND"))'

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + via + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied ACC-block GND stitch to {P}')
