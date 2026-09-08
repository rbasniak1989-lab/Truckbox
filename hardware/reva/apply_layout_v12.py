from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')
net_id={name:int(i) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)}

# Rev.A v12: remove only v11 routes implicated by DRC and reroute a subset
# on B.Cu. Critical F.Cu power/CAN/RF geometry stays untouched.
problem_nets={
    'GNSS_ON','USB_D-_LINK_MCU','USB_D+_LINK_MCU','CORE_BOOT',
    'FRAM_WP','FRAM_HOLD','WDT_REXT','ACC_BASE','LINK_EN','LINK_BOOT',
    'CAN1_TX_SAFE'
}
problem_ids={net_id[n] for n in problem_nets}

new=[]
for line in s.splitlines():
    if line.startswith('  (segment '):
        m=re.search(r'\(net (\d+)\)',line)
        if m and int(m.group(1)) in problem_ids:
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

def broute(net,p1,v1,path,v2,p2,w=.25):
    r=[seg(net,p1[0],p1[1],v1[0],v1[1],w,'F.Cu'),via(net,*v1)]
    cur=v1
    for pt in path:
        r.append(seg(net,cur[0],cur[1],pt[0],pt[1],w,'B.Cu')); cur=pt
    r += [seg(net,cur[0],cur[1],v2[0],v2[1],w,'B.Cu'),via(net,*v2),
          seg(net,v2[0],v2[1],p2[0],p2[1],w,'F.Cu')]
    return r

r=[]
# CORE_BOOT: U1 pin15 -> R29 pad2. Keep the via exits away from R29's 3V3 pad.
r += broute('CORE_BOOT',(26.75,24.45),(27.8,23.2),[(31.8,23.2)],(31.8,24.8),(30.5,25.0),.22)

# FRAM static control nets: escape outward from the SOIC, route on the back.
r += broute('FRAM_WP',(28.3,36.135),(26.6,36.2),[(26.6,29.0)],(32.8,29.0),(31.5,30.0),.22)
r += broute('FRAM_HOLD',(33.7,34.865),(35.5,35.0),[(36.2,31.5)],(36.2,29.4),(34.5,30.0),.22)

# Watchdog timing resistor.
r += broute('WDT_REXT',(20.9,35.05),(19.2,33.8),[(16.0,34.0)],(16.0,39.0),(17.5,39.0),.22)

# CAN1 TX safety pull-up. This signal never reaches the MCU; it only biases TXD recessive.
r += broute('CAN1_TX_SAFE',(69.3,40.595),(68.0,39.3),[(65.0,39.3)],(65.0,44.0),(67.0,44.0),.22)

# Link enable. R14 and C20 share LINK_EN; close their local branch first, then U2 on B.Cu.
r += [seg('LINK_EN',69.5,16.0,68.5,17.2,.22,'F.Cu'),
      seg('LINK_EN',68.5,17.2,68.5,19.0,.22,'F.Cu')]
r += broute('LINK_EN',(73.25,10.48),(71.6,10.4),[(67.0,12.0)],(67.0,15.0),(69.5,16.0),.22)

# Link BOOT: U2 -> R30, with a branch to SW1 on B.Cu.
r += broute('LINK_BOOT',(90.75,24.45),(91.2,26.0),[(96.0,26.0)],(96.0,24.5),(94.5,24.5),.22)
r += [via('LINK_BOOT',96.0,20.5),seg('LINK_BOOT',96.0,20.5,96.0,24.5,.22,'B.Cu'),
      seg('LINK_BOOT',96.0,20.5,94.5,20.5,.22,'F.Cu')]

# GNSS ON/OFF: escape upward from U5 pin5, then back-layer route to R41.
r += broute('GNSS_ON',(54.85,17.0),(55.3,15.4),[(60.5,15.4)],(60.5,20.0),(59.5,20.0),.22)

# USB MCU-side only. Downstream resistor-to-testpad routes from v11 were clean.
r += broute('USB_D-_LINK_MCU',(73.25,23.18),(72.0,21.7),[(66.2,21.7)],(66.2,23.0),(67.5,23.0),.20)
r += broute('USB_D+_LINK_MCU',(73.25,24.45),(72.0,27.0),[(66.2,27.0)],(66.2,25.0),(67.5,25.0),.20)

# ACC_BASE is intentionally left unrouted in this pass; its four-way local tree
# needs a dedicated compact topology rather than another crowded F.Cu shortcut.

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v12 B.Cu local reroute to {P}')
