from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')
net_id={name:int(i) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)}

# CURRENT PASS
# - remove legacy GND via at (62,11) below the Link-power corridor
# - keep WDT_3V3 clean
# - complete 3V3_LINK with verified pad escapes
# - route CAN1_RX/CAN2_RX + diagnostic TP3/TP4
# - bridge CAN2_RX over the CAN1_RX crossing on F.Cu

rebuild={'WDT_3V3','3V3_LINK','CAN1_RX','CAN2_RX'}
ids={net_id[n] for n in rebuild}
new=[]
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m=re.search(r'\(net (\d+)\)',line)
        if m and int(m.group(1)) in ids:
            continue
        if line.startswith('  (via '):
            at=re.search(r'\(at ([-0-9.]+) ([-0-9.]+)\)',line)
            if at and abs(float(at.group(1))-62.0)<1e-6 and abs(float(at.group(2))-11.0)<1e-6:
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

# WDT_3V3: route around WDT_REXT.
r += [
    seg('WDT_3V3',20.9,36.95,19.2,36.95,.30), via('WDT_3V3',19.2,36.95),
    seg('WDT_3V3',17.5,36.0,14.5,36.0,.30), via('WDT_3V3',14.5,36.0),
    seg('WDT_3V3',23.05,31.5,24.8,31.5,.30), via('WDT_3V3',24.8,31.5),
    seg('WDT_3V3',14.5,36.0,14.5,40.5,.40,'B.Cu'),
    seg('WDT_3V3',14.5,40.5,19.2,40.5,.40,'B.Cu'),
    seg('WDT_3V3',19.2,40.5,19.2,36.95,.40,'B.Cu'),
    seg('WDT_3V3',19.2,36.95,24.8,31.5,.40,'B.Cu'),
]

# 3V3_LINK: Q1 -> Link ESP32 and local Link decoupling.
r += [
    seg('3V3_LINK',68.05,7.5,69.5,7.5,.40), via('3V3_LINK',69.5,7.5),
    seg('3V3_LINK',73.25,9.21,71.5,9.21,.35), via('3V3_LINK',71.5,9.21),
    seg('3V3_LINK',69.5,7.5,71.5,9.21,.55,'B.Cu'),
    seg('3V3_LINK',68.5,10.5,66.5,10.5,.35),
    seg('3V3_LINK',68.4,13.0,66.5,13.0,.35),
    seg('3V3_LINK',68.5,16.0,66.5,16.0,.35),
    seg('3V3_LINK',66.5,10.5,66.5,16.0,.45),
    via('3V3_LINK',66.5,12.0),
    seg('3V3_LINK',69.5,7.5,66.5,9.0,.55,'B.Cu'),
    seg('3V3_LINK',66.5,9.0,66.5,12.0,.55,'B.Cu'),
    seg('3V3_LINK',54.85,13.7,57.0,13.7,.35),
    seg('3V3_LINK',57.0,13.7,58.5,13.5,.35),
    via('3V3_LINK',57.0,13.7),
    seg('3V3_LINK',57.0,13.7,60.0,11.5,.55,'B.Cu'),
    seg('3V3_LINK',60.0,11.5,66.5,9.0,.55,'B.Cu'),
    seg('3V3_LINK',59.4,17.0,59.4,15.3,.30), via('3V3_LINK',59.4,15.3),
    seg('3V3_LINK',59.4,15.3,57.0,13.7,.45,'B.Cu'),
    seg('3V3_LINK',58.5,20.0,57.2,20.0,.28),
    seg('3V3_LINK',57.2,20.0,57.2,18.3,.28),
    seg('3V3_LINK',57.2,18.3,59.4,18.3,.28),
    seg('3V3_LINK',59.4,18.3,59.4,17.0,.28),
    seg('3V3_LINK',93.5,24.5,93.5,22.8,.30), via('3V3_LINK',93.5,22.8),
    seg('3V3_LINK',93.5,22.8,93.5,9.5,.45,'B.Cu'),
    seg('3V3_LINK',93.5,9.5,71.5,9.21,.45,'B.Cu'),
]

# CAN1_RX: Core -> CAN1 transceiver plus TP3.
r += [
    seg('CAN1_RX',26.75,23.18,29.2,23.18,.22), via('CAN1_RX',29.2,23.18),
    seg('CAN1_RX',69.3,44.405,66.8,44.405,.22), via('CAN1_RX',66.8,44.405),
    seg('CAN1_RX',29.2,23.18,60.5,23.18,.25,'B.Cu'),
    seg('CAN1_RX',60.5,23.18,60.5,44.405,.25,'B.Cu'),
    seg('CAN1_RX',60.5,44.405,66.8,44.405,.25,'B.Cu'),
    seg('CAN1_RX',29.2,23.18,11.0,23.18,.22,'B.Cu'),
    seg('CAN1_RX',11.0,23.18,11.0,58.8,.22,'B.Cu'),
    seg('CAN1_RX',11.0,58.8,14.0,58.8,.22,'B.Cu'),
    via('CAN1_RX',14.0,58.8),
    seg('CAN1_RX',14.0,58.8,14.0,61.0,.22),
]

# CAN2_RX: the vertical trunk briefly moves to F.Cu from y42.5..46.5 so it
# crosses the CAN1_RX B.Cu horizontal without copper intersection.
r += [
    seg('CAN2_RX',26.75,20.64,28.0,20.64,.22), via('CAN2_RX',28.0,20.64),
    seg('CAN2_RX',69.3,50.405,69.3,52.2,.22), via('CAN2_RX',69.3,52.2),
    seg('CAN2_RX',28.0,20.64,28.0,21.0,.25,'B.Cu'),
    seg('CAN2_RX',28.0,21.0,64.5,21.0,.25,'B.Cu'),
    seg('CAN2_RX',64.5,21.0,64.5,42.5,.25,'B.Cu'),
    via('CAN2_RX',64.5,42.5),
    seg('CAN2_RX',64.5,42.5,64.5,46.5,.22,'F.Cu'),
    via('CAN2_RX',64.5,46.5),
    seg('CAN2_RX',64.5,46.5,64.5,52.2,.25,'B.Cu'),
    seg('CAN2_RX',64.5,52.2,69.3,52.2,.25,'B.Cu'),
    seg('CAN2_RX',28.0,21.0,7.5,21.0,.22,'B.Cu'),
    seg('CAN2_RX',7.5,21.0,7.5,63.0,.22,'B.Cu'),
    seg('CAN2_RX',7.5,63.0,17.0,63.0,.22,'B.Cu'),
    via('CAN2_RX',17.0,63.0),
    seg('CAN2_RX',17.0,63.0,17.0,61.0,.22),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied current routing pass to {P}')
