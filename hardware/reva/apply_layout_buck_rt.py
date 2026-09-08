from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

rebuild = {'BUCK_RT'}
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

# BUCK_RT: U6 pin 5 (60.15,57.00) -> R8 pad 1 (55.50,64.00).
# Escape upward from the fine-pitch TPS pin before changing layer. B.Cu is
# unused in this local power block, so route around the four exposed-pad GND
# vias on their right/bottom side, then return to F.Cu left of R8.
r = [
    seg('BUCK_RT', 60.15, 57.00, 60.15, 55.20),
    via('BUCK_RT', 60.15, 55.20),
    seg('BUCK_RT', 60.15, 55.20, 62.00, 55.20, .22, 'B.Cu'),
    seg('BUCK_RT', 62.00, 55.20, 62.00, 62.50, .22, 'B.Cu'),
    seg('BUCK_RT', 62.00, 62.50, 54.50, 62.50, .22, 'B.Cu'),
    seg('BUCK_RT', 54.50, 62.50, 54.50, 64.00, .22, 'B.Cu'),
    via('BUCK_RT', 54.50, 64.00),
    seg('BUCK_RT', 54.50, 64.00, 55.50, 64.00),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied BUCK_RT routing pass to {P}')
