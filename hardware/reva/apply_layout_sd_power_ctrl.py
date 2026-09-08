from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

# microSD switched-rail gate control. Own only SD_PWR_N.
rebuild = {'SD_PWR_N'}
ids = {net_id[n] for n in rebuild}
new = []
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m = re.search(r'\(net (\d+)\)', line)
        if m and int(m.group(1)) in ids:
            continue
    new.append(line)
s = '\n'.join(new) + '\n'

def seg(net, x1, y1, x2, y2, w=.22, layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {net_id[net]}))')

def via(net, x, y, size=.70, drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") (net {net_id[net]}))')

r = []

# SD_PWR_N endpoints:
# U1 GPIO17/pad24 (26.75,13.02) -> R27.2 (55.5,30.0)
# -> Q2 gate/pad1 (54.45,33.95).
# Escape the ESP32 edge on F.Cu, drop to B.Cu at x=28.5 and move to y=14.8
# so this trunk stays clear of LINK_PWR_N/SD_CS. Return to F.Cu to approach
# R27 from the right, avoiding its adjacent 3V3_MAIN pad.
r += [
    seg('SD_PWR_N', 26.75, 13.02, 28.5, 13.02),
    via('SD_PWR_N', 28.5, 13.02),
    seg('SD_PWR_N', 28.5, 13.02, 28.5, 14.8, .22, 'B.Cu'),
    seg('SD_PWR_N', 28.5, 14.8, 57.5, 14.8, .22, 'B.Cu'),
    seg('SD_PWR_N', 57.5, 14.8, 57.5, 30.0, .22, 'B.Cu'),
    via('SD_PWR_N', 57.5, 30.0),
    seg('SD_PWR_N', 57.5, 30.0, 55.5, 30.0),
]

# R27 -> Q2 gate. Go around the right/bottom of Q2 so its source/drain pads
# (3V3_MAIN and 3V3_SD) are never crossed.
r += [
    seg('SD_PWR_N', 55.5, 30.0, 57.8, 30.0),
    seg('SD_PWR_N', 57.8, 30.0, 57.8, 35.2),
    seg('SD_PWR_N', 57.8, 35.2, 54.45, 35.2),
    seg('SD_PWR_N', 54.45, 35.2, 54.45, 33.95),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied SD_PWR_N routing pass to {P}')
