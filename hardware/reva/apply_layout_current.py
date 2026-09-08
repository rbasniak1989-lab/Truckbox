from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')
net_id={name:int(i) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)}

# CURRENT PASS
# - fix the last WDT power crossing reported by KiCad 10
# - establish the first 3V3_LINK power bus (Q1/U2 + local decoupling + U5/C14)

rebuild={'WDT_3V3','3V3_LINK'}
ids={net_id[n] for n in rebuild}
new=[]
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m=re.search(r'\(net (\d+)\)',line)
        if m and int(m.group(1)) in ids:
            continue
    new.append(line)
s='\n'.join(new)+'\n'


def seg(net,x1,y1,x2,y2,w=.25,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {net_id[net]}))')

def via(net,x,y,size=.70,drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") (net {net_id[net]}))')

r=[]
# WDT_3V3: REXT uses x=16 on B.Cu from y34..39, therefore route power around
# its lower end at y40.5 instead of crossing it.
r += [
    seg('WDT_3V3',20.9,36.95,19.2,36.95,.30), via('WDT_3V3',19.2,36.95),
    seg('WDT_3V3',17.5,36.0,14.5,36.0,.30), via('WDT_3V3',14.5,36.0),
    seg('WDT_3V3',23.05,31.5,24.8,31.5,.30), via('WDT_3V3',24.8,31.5),
    seg('WDT_3V3',14.5,36.0,14.5,40.5,.40,'B.Cu'),
    seg('WDT_3V3',14.5,40.5,19.2,40.5,.40,'B.Cu'),
    seg('WDT_3V3',19.2,40.5,19.2,36.95,.40,'B.Cu'),
    seg('WDT_3V3',19.2,36.95,24.8,31.5,.40,'B.Cu'),
]

# 3V3_LINK - top-right cluster.
# Q1 source/output pad3 -> power bus.
r += [
    seg('3V3_LINK',68.05,7.5,69.5,7.5,.40), via('3V3_LINK',69.5,7.5),
    # U2 supply pin2 exits straight left.
    seg('3V3_LINK',73.25,9.21,71.5,9.21,.35), via('3V3_LINK',71.5,9.21),
    seg('3V3_LINK',69.5,7.5,71.5,9.21,.55,'B.Cu'),
]

# C10/C16/R14 have their 3V3 pads on the left. Tie them to a local F.Cu rail
# and drop one via into the B.Cu power bus.
r += [
    seg('3V3_LINK',68.5,10.5,66.5,10.5,.35),
    seg('3V3_LINK',68.4,13.0,66.5,13.0,.35),
    seg('3V3_LINK',68.5,16.0,66.5,16.0,.35),
    seg('3V3_LINK',66.5,10.5,66.5,16.0,.45),
    via('3V3_LINK',66.5,12.0),
    seg('3V3_LINK',69.5,7.5,66.5,9.0,.55,'B.Cu'),
    seg('3V3_LINK',66.5,9.0,66.5,12.0,.55,'B.Cu'),
]

# GNSS main supply U5 pin8 and its nearby decoupling C14 share a left-side
# local rail, then join the same back-layer bus. C17/R41 and remote R30 remain
# for the next pass to avoid crowding GNSS_ON and LINK_BOOT routes.
r += [
    seg('3V3_LINK',54.85,13.7,57.0,13.7,.35),
    seg('3V3_LINK',57.0,13.7,58.5,13.5,.35),
    via('3V3_LINK',57.0,13.7),
    seg('3V3_LINK',57.0,13.7,60.0,11.5,.55,'B.Cu'),
    seg('3V3_LINK',60.0,11.5,66.5,9.0,.55,'B.Cu'),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied current routing pass to {P}')
