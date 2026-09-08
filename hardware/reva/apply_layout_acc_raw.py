from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

# Raw ACC input from truck connector to the first divider resistor.
rebuild = {'ACC_RAW'}
ids = {net_id[n] for n in rebuild}
new = []
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m = re.search(r'\(net (\d+)\)', line)
        if m and int(m.group(1)) in ids:
            continue
    new.append(line)
s = '\n'.join(new) + '\n'

def seg(net, x1, y1, x2, y2, w=.28, layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {net_id[net]}))')

r = []

# ACC_RAW: R10.1 (91.5,35.0) -> J4.3 (97.8,45.5).
# Escape upward from R10 so the adjacent R10.2 / ACC_MID1 pad at (92.5,35)
# is never crossed, then use the open connector-side corridor at x=96 mm.
r += [
    seg('ACC_RAW', 91.5, 35.0, 91.5, 33.0),
    seg('ACC_RAW', 91.5, 33.0, 96.0, 33.0),
    seg('ACC_RAW', 96.0, 33.0, 96.0, 45.5),
    seg('ACC_RAW', 96.0, 45.5, 97.8, 45.5),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied ACC_RAW routing pass to {P}')
