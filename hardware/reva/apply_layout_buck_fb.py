from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^\s*\(net (\d+) "([^"]+)"\)\s*$', s, re.M)}

fb = net_id['FB_3V3']
# Rebuild only FB_3V3 so this pass is deterministic.
new = []
for line in s.splitlines():
    if line.startswith('\t(segment') or line.startswith('\t(via') or line.startswith('  (segment') or line.startswith('  (via'):
        # KiCad 10 may serialize nets by name rather than numeric id; catch both forms.
        if '(net "FB_3V3")' in line or f'(net {fb})' in line:
            continue
    new.append(line)
s = '\n'.join(new) + '\n'

def seg(x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return (f'\t(segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "FB_3V3"))')

def via(x,y,size=.60,drill=.30):
    return (f'\t(via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "FB_3V3"))')

r = [
    # Divider itself: R6.2=(53.5,51.5) -> R7.1=(52.5,53.5).
    seg(53.50,51.50,52.50,53.50,.22),

    # U6 VSENSE/pin7=(55.85,57.50). Escape straight left, then put the
    # sensitive feedback trace on B.Cu while it passes the switching region.
    # L2 is the solid GND plane, shielding this route from F.Cu SW_NODE.
    seg(55.85,57.50,54.55,57.50,.20),
    via(54.55,57.50),
    seg(54.55,57.50,53.20,56.20,.20,'B.Cu'),
    seg(53.20,56.20,52.00,54.30,.20,'B.Cu'),
    via(52.00,54.30),
    # Short F.Cu finish directly into the lower-divider pad.
    seg(52.00,54.30,52.50,53.50,.20),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied FB_3V3 routing pass to {P}')
