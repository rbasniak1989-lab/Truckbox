from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rebuild FB_3V3 and COMP together so the adjacent TPS54260 analog pins have
# deliberately separated escapes. L1 has already been shifted left by 1 mm.
rebuild={'FB_3V3','COMP'}
new=[]
for line in s.splitlines():
    if line.lstrip().startswith('(segment ') or line.lstrip().startswith('(via '):
        if any(f'(net "{n}")' in line for n in rebuild):
            continue
    new.append(line)
s='\n'.join(new)+'\n'

def seg(net,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "{net}"))')

def via(net,x,y,size=.60,drill=.30):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "{net}"))')

r=[]
# FB divider: R6.2=(53.5,51.5) and R7.1=(52.5,53.5).
r.append(seg('FB_3V3',53.50,51.50,52.50,53.50,.22))
# U6 pin7=(55.85,57.50). Exit straight left first, then angle upward only
# after clearing the adjacent pad row. Via at (54.2,56.9) is clear of L1.
r += [
    seg('FB_3V3',55.85,57.50,54.90,57.50,.20),
    seg('FB_3V3',54.90,57.50,54.20,56.90,.20),
    via('FB_3V3',54.20,56.90),
    seg('FB_3V3',54.20,56.90,53.10,55.80,.20,'B.Cu'),
    seg('FB_3V3',53.10,55.80,52.00,54.30,.20,'B.Cu'),
    via('FB_3V3',52.00,54.30),
    seg('FB_3V3',52.00,54.30,52.50,53.50,.20),
]

# COMP: U6 pin8=(55.85,58.00) -> R9.1=(60.50,47.00).
# Exit horizontally at y=58 first to preserve clearance to U6 pin9/GND,
# then place a separate via at (54.6,58.2). The B.Cu route heads up/right,
# away from FB, SW_NODE and the local input-power copper.
r += [
    seg('COMP',55.85,58.00,54.90,58.00,.20),
    seg('COMP',54.90,58.00,54.60,58.20,.20),
    via('COMP',54.60,58.20),
    seg('COMP',54.60,58.20,55.50,55.80,.20,'B.Cu'),
    seg('COMP',55.50,55.80,58.50,50.00,.20,'B.Cu'),
    seg('COMP',58.50,50.00,59.50,47.00,.20,'B.Cu'),
    via('COMP',59.50,47.00),
    seg('COMP',59.50,47.00,60.50,47.00,.20),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied separated FB_3V3 + COMP analog escape pass to {P}')
