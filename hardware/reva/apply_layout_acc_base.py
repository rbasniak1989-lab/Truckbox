from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

# Local ACC transistor-base network. Rebuild only ACC_BASE so every previously
# validated power/CAN/SPI/control route remains untouched.
rebuild = {'ACC_BASE'}
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

r = []

# ACC_BASE endpoints:
# R11.1 (83.5,37.0) -> Q3.1 (86.95,37.95)
# -> D7.2 (88.05,40.0) -> R28.2 (92.5,37.0).
# Short top-copper jogs avoid adjacent pads from ACC_N/GND and keep this small
# analog/input network entirely local to the right-side ACC conditioning block.
r += [
    seg('ACC_BASE', 83.5, 37.0, 84.0, 38.0),
    seg('ACC_BASE', 84.0, 38.0, 86.0, 38.0),
    seg('ACC_BASE', 86.0, 38.0, 86.95, 37.95),
    seg('ACC_BASE', 86.95, 37.95, 88.05, 40.0),
    seg('ACC_BASE', 88.05, 40.0, 89.05, 39.0),
    seg('ACC_BASE', 89.05, 39.0, 89.05, 38.0),
    seg('ACC_BASE', 89.05, 38.0, 92.5, 38.0),
    seg('ACC_BASE', 92.5, 38.0, 92.5, 37.0),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied ACC_BASE routing pass to {P}')
