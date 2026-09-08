from pathlib import Path

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rebuild PWRGD_TP only. Rev.A uses PWRGD as a service/test signal; no logic
# dependency is added here.
new=[]
for line in s.splitlines():
    if line.lstrip().startswith('(segment ') or line.lstrip().startswith('(via '):
        if '(net "PWRGD_TP")' in line:
            continue
    new.append(line)
s='\n'.join(new)+'\n'

def seg(x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "PWRGD_TP"))')

def via(x,y,size=.60,drill=.30):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "PWRGD_TP"))')

r=[
    # U6 pin6=(55.85,57.00). Keep a short local F.Cu escape and change layer
    # at (54.6,56.1). This point is >0.6 mm center-distance from the COMP B.Cu
    # trace and remains well clear of FB and the shifted L1 SW pad.
    seg(55.85,57.00,55.20,57.00,.20),
    seg(55.20,57.00,54.60,56.10,.20),
    via(54.60,56.10),

    # Reuse the service corridor that was already clean in the earlier pass.
    seg(54.60,56.10,54.60,53.00,.20,'B.Cu'),
    seg(54.60,53.00,46.00,53.00,.20,'B.Cu'),
    seg(46.00,53.00,46.00,57.50,.20,'B.Cu'),
    seg(46.00,57.50,34.00,59.50,.20,'B.Cu'),
    seg(34.00,59.50,23.00,59.50,.20,'B.Cu'),
    via(23.00,59.50),
    seg(23.00,59.50,23.00,61.00,.20),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied local COMP/L1-clear PWRGD_TP routing pass to {P}')
