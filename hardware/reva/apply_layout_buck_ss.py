from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

# BUCK_SS endpoint geometry:
# U6 pin 4 = (60.15,57.50), C5 pad 1 = (61.50,63.00).
# The original local VIN_PROT dogleg at x=61.4 blocks a manufacturable escape
# from the 0.5-mm pitch SS pin. Move only that final VIN dogleg to x=63.0;
# all upstream VIN copper remains unchanged.
old_vin = [
    '  (segment (start 62.200 56.200) (end 61.400 57.200) (width 0.280) (layer "F.Cu") (net 3))',
    '  (segment (start 61.400 57.200) (end 61.400 58.500) (width 0.220) (layer "F.Cu") (net 3))',
    '  (segment (start 61.400 58.500) (end 60.150 58.500) (width 0.200) (layer "F.Cu") (net 3))',
]
for line in old_vin:
    if line not in s:
        raise RuntimeError(f'expected VIN_PROT segment missing: {line}')
    s = s.replace(line + '\n', '', 1)

# Remove any previous BUCK_SS route so this pass is idempotent.
ss_id = net_id['BUCK_SS']
new = []
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m = re.search(r'\(net (\d+)\)', line)
        if m and int(m.group(1)) == ss_id:
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
    # Revised final VIN_PROT approach. Keep the 0.20-mm fine-pitch escape at U6.
    seg('VIN_PROT', 62.20, 56.20, 63.00, 57.00, .28),
    seg('VIN_PROT', 63.00, 57.00, 63.00, 58.50, .22),
    seg('VIN_PROT', 63.00, 58.50, 60.15, 58.50, .20),

    # Soft-start: escape laterally beyond adjacent TPS pads, then use B.Cu.
    # x=61.30 keeps the vias clear of both adjacent U6 pins and BUCK_RT x=62.
    seg('BUCK_SS', 60.15, 57.50, 61.30, 57.50, .20),
    via('BUCK_SS', 61.30, 57.50),
    seg('BUCK_SS', 61.30, 57.50, 61.30, 61.50, .22, 'B.Cu'),
    via('BUCK_SS', 61.30, 61.50),
    seg('BUCK_SS', 61.30, 61.50, 61.30, 63.00, .22),
    seg('BUCK_SS', 61.30, 63.00, 61.50, 63.00, .22),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied BUCK_SS routing + local VIN clearance pass to {P}')
