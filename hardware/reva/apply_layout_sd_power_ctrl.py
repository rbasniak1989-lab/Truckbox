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
#
# Revised route after real KiCad DRC of the first pass:
# - move away from LINK_PWR_N before changing layer;
# - use a clear B.Cu horizontal corridor at y=14.2, above CAN1/CAN2;
# - return to F.Cu at x=50 so CAN RX tracks can be crossed on the opposite layer;
# - approach R27 pad 2 from the RIGHT, never crossing R27 pad 1 (3V3_MAIN).
r += [
    seg('SD_PWR_N', 26.75, 13.02, 28.50, 13.02),
    seg('SD_PWR_N', 28.50, 13.02, 28.50, 14.20),
    via('SD_PWR_N', 28.50, 14.20),
    seg('SD_PWR_N', 28.50, 14.20, 50.00, 14.20, .22, 'B.Cu'),
    via('SD_PWR_N', 50.00, 14.20),
    seg('SD_PWR_N', 50.00, 14.20, 50.00, 28.50),
    seg('SD_PWR_N', 50.00, 28.50, 57.00, 28.50),
    seg('SD_PWR_N', 57.00, 28.50, 57.00, 30.00),
    seg('SD_PWR_N', 57.00, 30.00, 55.50, 30.00),
]

# R27 -> Q2 gate. Exit R27 pad 2 to the right, pass ABOVE Q2, then go
# around the LEFT side of Q2 pad 2 (3V3_MAIN) and enter gate pad 1 from left.
# This avoids both Q2 source/drain copper and the existing 3V3_SD via/track.
r += [
    seg('SD_PWR_N', 55.50, 30.00, 56.30, 30.00),
    seg('SD_PWR_N', 56.30, 30.00, 56.30, 31.00),
    seg('SD_PWR_N', 56.30, 31.00, 53.50, 31.00),
    seg('SD_PWR_N', 53.50, 31.00, 53.50, 33.95),
    seg('SD_PWR_N', 53.50, 33.95, 54.45, 33.95),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied SD_PWR_N routing pass to {P}')
