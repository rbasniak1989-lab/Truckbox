from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')
net_id={name:int(i) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)}

# Rev.A v13 - DRC-driven pad escapes.  For fine-pitch/LCC/module pads, leave
# the pad straight along its centerline before changing direction on B.Cu.
fix_nets={'CORE_BOOT','CAN1_TX_SAFE','LINK_EN','GNSS_ON','USB_D-_LINK_MCU'}
ids={net_id[n] for n in fix_nets}
new=[]
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m=re.search(r'\(net (\d+)\)',line)
        if m and int(m.group(1)) in ids:
            continue
    new.append(line)
s='\n'.join(new)+'\n'


def seg(net,x1,y1,x2,y2,w=.22,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {net_id[net]}))')

def via(net,x,y,size=.70,drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") (net {net_id[net]}))')

r=[]
# CORE_BOOT: straight right from module pad15, then below R29 on B.Cu.
r += [
    seg('CORE_BOOT',26.75,24.45,28.5,24.45), via('CORE_BOOT',28.5,24.45),
    seg('CORE_BOOT',28.5,24.45,32.0,26.2,.22,'B.Cu'), via('CORE_BOOT',32.0,26.2),
    seg('CORE_BOOT',32.0,26.2,30.5,25.0),
]

# CAN1 TX recessive pull-up: leave U3 pin1 to the left; approach R3 pad2 from right.
r += [
    seg('CAN1_TX_SAFE',69.3,40.595,67.4,40.595), via('CAN1_TX_SAFE',67.4,40.595),
    seg('CAN1_TX_SAFE',67.4,40.595,68.3,45.3,.22,'B.Cu'), via('CAN1_TX_SAFE',68.3,45.3),
    seg('CAN1_TX_SAFE',68.3,45.3,67.0,44.0),
]

# LINK_EN U2 pin3: straight left. R14 pad2 is approached from the right.
r += [
    seg('LINK_EN',73.25,10.48,71.0,10.48), via('LINK_EN',71.0,10.48),
    seg('LINK_EN',71.0,10.48,71.0,16.0,.22,'B.Cu'), via('LINK_EN',71.0,16.0),
    seg('LINK_EN',71.0,16.0,69.5,16.0),
    # R14 pad2 -> C20 pad1 on B.Cu, entering C20 from the left.
    seg('LINK_EN',69.5,16.0,70.8,16.0), via('LINK_EN',70.8,16.0),
    seg('LINK_EN',70.8,16.0,67.0,19.0,.22,'B.Cu'), via('LINK_EN',67.0,19.0),
    seg('LINK_EN',67.0,19.0,68.5,19.0),
]

# GNSS_ON: U5 pin5 is an edge pad; go straight right beyond all LCC pads.
r += [
    seg('GNSS_ON',54.85,17.0,57.0,17.0), via('GNSS_ON',57.0,17.0),
    seg('GNSS_ON',57.0,17.0,61.0,20.0,.22,'B.Cu'), via('GNSS_ON',61.0,20.0),
    seg('GNSS_ON',61.0,20.0,59.5,20.0),
]

# USB D-: U2 pin13 exits left on its own centerline.  R42 pad1 approached from left.
r += [
    seg('USB_D-_LINK_MCU',73.25,23.18,70.5,23.18,.20), via('USB_D-_LINK_MCU',70.5,23.18),
    seg('USB_D-_LINK_MCU',70.5,23.18,66.0,23.0,.20,'B.Cu'), via('USB_D-_LINK_MCU',66.0,23.0),
    seg('USB_D-_LINK_MCU',66.0,23.0,67.5,23.0,.20),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v13 straight pad escapes to {P}')
