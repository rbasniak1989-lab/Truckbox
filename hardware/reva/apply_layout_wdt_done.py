from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

rebuild = {'WDT_DONE'}
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

# WDT_DONE endpoints:
# U1 GPIO1/pad9 (9.25,18.10) -> U8 DONE/pad4 (23.10,35.05).
#
# DRC-corrected route:
# 1) the first via is kept at x=8.0, well clear of C19 GND pad at (6,18),
#    then B.Cu moves left to x=6.5 before descending parallel to CAN2_RX;
# 2) near U8 the route detours DOWN to y=32.9, passing below the WDT_REXT
#    via/diagonal and left of Q4 ACC_N pad, then returns to y=33.5.
r += [
    seg('WDT_DONE', 9.25, 18.10, 8.00, 18.10),
    via('WDT_DONE', 8.00, 18.10),
    seg('WDT_DONE', 8.00, 18.10, 6.50, 18.10, .22, 'B.Cu'),
    seg('WDT_DONE', 6.50, 18.10, 6.50, 33.50, .22, 'B.Cu'),
    via('WDT_DONE', 6.50, 33.50),
    seg('WDT_DONE', 6.50, 33.50, 18.00, 33.50),
    seg('WDT_DONE', 18.00, 33.50, 18.00, 32.90),
    seg('WDT_DONE', 18.00, 32.90, 20.00, 32.90),
    seg('WDT_DONE', 20.00, 32.90, 20.00, 33.50),
    seg('WDT_DONE', 20.00, 33.50, 24.50, 33.50),
    seg('WDT_DONE', 24.50, 33.50, 24.50, 35.05),
    seg('WDT_DONE', 24.50, 35.05, 23.10, 35.05),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied WDT_DONE routing pass to {P}')
