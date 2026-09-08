from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

# Storage chip-select routing pass. Rebuild only the nets owned by this pass so
# previous validated CAN/SPI geometry remains untouched.
rebuild = {'FRAM_CS'}
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

# FRAM_CS: U1 GPIO11/pad11 (9.25,20.64) -> U7 /CS pad1 (28.3,33.595)
# plus R24 pull-up pad2 (28.5,30.0).
# Stay on F.Cu while crossing the vertical CAN/SPI trunks on B.Cu, switch to a
# clear B.Cu vertical at x=22.5, then return to F.Cu above the FRAM.
r += [
    seg('FRAM_CS', 9.25, 20.64, 4.2, 20.64),
    seg('FRAM_CS', 4.2, 20.64, 4.2, 26.5),
    seg('FRAM_CS', 4.2, 26.5, 22.5, 26.5),
    via('FRAM_CS', 22.5, 26.5),
    seg('FRAM_CS', 22.5, 26.5, 22.5, 32.5, .22, 'B.Cu'),
    via('FRAM_CS', 22.5, 32.5),
    seg('FRAM_CS', 22.5, 32.5, 28.3, 32.5),
    seg('FRAM_CS', 28.3, 32.5, 28.3, 33.595),
    seg('FRAM_CS', 28.3, 33.595, 28.5, 30.0),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied FRAM_CS storage routing pass to {P}')
