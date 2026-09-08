from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

rebuild = {'LINK_PWR_N'}
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

# LINK_PWR_N: U1 GPIO16/pad25 (26.75,11.75) -> R26 pad2 (64.5,9.0)
# -> Q1 gate pad1 (65.95,8.45). Escape outward before changing layer, keep
# clear of SD_CS, then use the open top-side corridor. Approach R26 from above
# so the adjacent 3V3_MAIN pad at (63.5,9.0) is never crossed.
r += [
    seg('LINK_PWR_N', 26.75, 11.75, 28.0, 11.75),
    via('LINK_PWR_N', 28.0, 11.75),
    seg('LINK_PWR_N', 28.0, 11.75, 28.0, 13.0, .22, 'B.Cu'),
    seg('LINK_PWR_N', 28.0, 13.0, 31.0, 13.0, .22, 'B.Cu'),
    seg('LINK_PWR_N', 31.0, 13.0, 31.0, 7.5, .22, 'B.Cu'),
    via('LINK_PWR_N', 31.0, 7.5),
    seg('LINK_PWR_N', 31.0, 7.5, 64.5, 7.5),
    seg('LINK_PWR_N', 64.5, 7.5, 64.5, 9.0),
    seg('LINK_PWR_N', 64.5, 9.0, 65.95, 8.45),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied LINK_PWR_N routing pass to {P}')
