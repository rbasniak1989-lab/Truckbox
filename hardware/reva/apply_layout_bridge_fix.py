from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')
net_id={name:int(i) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)}
nid=net_id['CAN2_RX']

# Remove only CAN2_RX copper produced by apply_layout_current.py.
new=[]
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m=re.search(r'\(net (\d+)\)',line)
        if m and int(m.group(1))==nid:
            continue
    new.append(line)
s='\n'.join(new)+'\n'

def seg(x1,y1,x2,y2,w=.25,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {nid}))')

def via(x,y,size=.70,drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") (net {nid}))')

r=[
    # U1 pad18 escape, stopping before R20.
    seg(26.75,20.64,28.0,20.64,.22), via(28.0,20.64),
    # U4 pad4 escape downward, away from C2.
    seg(69.3,50.405,69.3,52.2,.22), via(69.3,52.2),
    # B.Cu trunk from Core to the crossing region.
    seg(28.0,20.64,28.0,21.0,.25,'B.Cu'),
    seg(28.0,21.0,64.5,21.0,.25,'B.Cu'),
    seg(64.5,21.0,64.5,42.5,.25,'B.Cu'),
    # Jog 1 mm right on B.Cu, bridge on F.Cu, then return left.
    seg(64.5,42.5,65.5,42.5,.25,'B.Cu'), via(65.5,42.5),
    seg(65.5,42.5,65.5,46.5,.22,'F.Cu'), via(65.5,46.5),
    seg(65.5,46.5,64.5,46.5,.25,'B.Cu'),
    seg(64.5,46.5,64.5,52.2,.25,'B.Cu'),
    seg(64.5,52.2,69.3,52.2,.25,'B.Cu'),
    # TP4 diagnostic branch.
    seg(28.0,21.0,7.5,21.0,.22,'B.Cu'),
    seg(7.5,21.0,7.5,63.0,.22,'B.Cu'),
    seg(7.5,63.0,17.0,63.0,.22,'B.Cu'), via(17.0,63.0),
    seg(17.0,63.0,17.0,61.0,.22),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied CAN2 bridge hotfix to {P}')
