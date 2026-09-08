from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')
net_id={name:int(i) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)}

# Final post-pass for the CAN control region:
# 1) rebuild CAN2_RX with its clean F.Cu bridge;
# 2) route CAN_MODE to U1, R20 and both control pins on U3/U4.
# CAN_MODE crosses the two CAN RX vertical trunks at y=30 mm, a verified
# pad/track-free F.Cu corridor, avoiding the Link USB service routing.
rebuild={'CAN2_RX','CAN_MODE'}
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

# CAN2_RX clean bridge over CAN1_RX.
r += [
    seg('CAN2_RX',26.75,20.64,28.0,20.64,.22), via('CAN2_RX',28.0,20.64),
    seg('CAN2_RX',69.3,50.405,69.3,52.2,.22), via('CAN2_RX',69.3,52.2),
    seg('CAN2_RX',28.0,20.64,28.0,21.0,.25,'B.Cu'),
    seg('CAN2_RX',28.0,21.0,64.5,21.0,.25,'B.Cu'),
    seg('CAN2_RX',64.5,21.0,64.5,42.5,.25,'B.Cu'),
    seg('CAN2_RX',64.5,42.5,65.5,42.5,.25,'B.Cu'), via('CAN2_RX',65.5,42.5),
    seg('CAN2_RX',65.5,42.5,65.5,46.5,.22,'F.Cu'), via('CAN2_RX',65.5,46.5),
    seg('CAN2_RX',65.5,46.5,64.5,46.5,.25,'B.Cu'),
    seg('CAN2_RX',64.5,46.5,64.5,52.2,.25,'B.Cu'),
    seg('CAN2_RX',64.5,52.2,69.3,52.2,.25,'B.Cu'),
    seg('CAN2_RX',28.0,21.0,7.5,21.0,.22,'B.Cu'),
    seg('CAN2_RX',7.5,21.0,7.5,63.0,.22,'B.Cu'),
    seg('CAN2_RX',7.5,63.0,17.0,63.0,.22,'B.Cu'), via('CAN2_RX',17.0,63.0),
    seg('CAN2_RX',17.0,63.0,17.0,61.0,.22),
]

# CAN_MODE local Core branch: U1 GPIO2/pin27 -> R20 pad2.
r += [
    seg('CAN_MODE',26.75,9.21,28.6,9.21,.22),
    seg('CAN_MODE',28.6,9.21,29.2,10.0,.22),
    seg('CAN_MODE',29.2,10.0,29.2,19.2,.22),
    seg('CAN_MODE',29.2,19.2,30.5,20.2,.22),
    seg('CAN_MODE',30.5,20.2,30.5,21.0,.22),
    seg('CAN_MODE',30.5,21.0,32.0,22.2,.22),
    seg('CAN_MODE',32.0,22.2,32.0,24.5,.22),
    seg('CAN_MODE',32.0,24.5,38.0,24.5,.22), via('CAN_MODE',38.0,24.5),
]

# Long CAN_MODE trunk. Move to the verified y=30 F.Cu crossing corridor.
r += [
    seg('CAN_MODE',38.0,24.5,52.0,24.5,.28,'B.Cu'),
    seg('CAN_MODE',52.0,24.5,58.5,30.0,.28,'B.Cu'),
    via('CAN_MODE',58.5,30.0),
    seg('CAN_MODE',58.5,30.0,66.5,30.0,.22,'F.Cu'),
    via('CAN_MODE',66.5,30.0),
    seg('CAN_MODE',66.5,30.0,78.0,30.0,.28,'B.Cu'),
    seg('CAN_MODE',78.0,30.0,78.0,40.595,.28,'B.Cu'),
]

# Both control pins of both TCAN3404s.
for y in (40.595,44.405,46.595,50.405):
    r += [
        seg('CAN_MODE',74.7,y,76.5,y,.22),
        via('CAN_MODE',76.5,y),
        seg('CAN_MODE',76.5,y,78.0,y,.28,'B.Cu'),
    ]
r += [seg('CAN_MODE',78.0,40.595,78.0,50.405,.28,'B.Cu')]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied CAN2 bridge + CAN_MODE routing to {P}')
