from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

# Rebuild BUCK_EN and BUCK_SS together. BUCK_SS's first via is moved up/left
# to open a clean F.Cu channel for U6 pin 3 (EN) between SS and VIN.
rebuild = {'BUCK_EN', 'BUCK_SS'}
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

r = [
    # BUCK_SS: U6 pin4 (60.15,57.50) -> C5.1 (61.50,63.00).
    # Straight lateral escape, then a 0.60/0.30 via at x=61.15. This clears
    # U6 pin5/BUCK_RT while still leaving room for the BUCK_EN x=61.8 channel.
    seg('BUCK_SS', 60.15, 57.50, 61.15, 57.50, .20),
    seg('BUCK_SS', 61.15, 57.50, 61.15, 56.20, .20),
    via('BUCK_SS', 61.15, 56.20, .60, .30),
    seg('BUCK_SS', 61.15, 56.20, 61.15, 61.50, .22, 'B.Cu'),
    via('BUCK_SS', 61.15, 61.50),
    seg('BUCK_SS', 61.15, 61.50, 61.15, 63.00, .22),
    seg('BUCK_SS', 61.15, 63.00, 61.50, 63.00, .22),

    # BUCK_EN divider: R31.2=(55.5,47), R32.1=(57.5,47).
    seg('BUCK_EN', 55.50, 47.00, 57.50, 47.00, .22),
    # Branch out through the courtyard gap between R31/R32, upward away from D5.
    seg('BUCK_EN', 56.50, 47.00, 56.50, 45.80, .22),
    via('BUCK_EN', 56.50, 45.80),
    # B.Cu corridor is clear below CAN1_RX (which ends near y=44.405).
    seg('BUCK_EN', 56.50, 45.80, 61.80, 45.80, .22, 'B.Cu'),
    seg('BUCK_EN', 61.80, 45.80, 61.80, 53.50, .22, 'B.Cu'),
    via('BUCK_EN', 61.80, 53.50),
    # Return to F.Cu left of C1; x=61.8 leaves >0.20 mm to C1 copper.
    seg('BUCK_EN', 61.80, 53.50, 61.80, 58.00, .20),
    seg('BUCK_EN', 61.80, 58.00, 60.15, 58.00, .20),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied BUCK_EN + revised BUCK_SS breakout pass to {P}')
