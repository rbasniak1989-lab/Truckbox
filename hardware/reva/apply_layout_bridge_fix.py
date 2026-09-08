from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')
net_id={name:int(i) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)}

# Final post-pass:
# 1) rebuild CAN2_RX with its clean F.Cu bridge;
# 2) route CAN_MODE to U1, R20 and both control pins on U3/U4;
# 3) remove legacy GND stitching vias on the y=30 CAN_MODE path;
# 4) rebuild SPI_CLK with an immediate layer change to bypass C15/R13/C19;
# 5) extend SPI_CLK/SPI_MOSI from FRAM to microSD using separated lanes;
# 6) route SPI_MISO Core -> FRAM -> microSD in a separate lower corridor.
rebuild={'CAN2_RX','CAN_MODE','SPI_CLK','SPI_MISO'}
ids={net_id[n] for n in rebuild}
legacy_stitch={(62.0,30.0),(76.0,30.0)}
new=[]
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m=re.search(r'\(net (\d+)\)',line)
        if m and int(m.group(1)) in ids:
            continue
        if line.startswith('  (via '):
            at=re.search(r'\(at ([-0-9.]+) ([-0-9.]+)\)',line)
            if at:
                pt=(float(at.group(1)),float(at.group(2)))
                if pt in legacy_stitch:
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

# Long CAN_MODE trunk using verified y=30 F.Cu crossing corridor.
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

# SPI_CLK: edge corridor around Core, then FRAM pad6.
r += [
    seg('SPI_CLK',9.25,14.29,7.7,14.29,.22),
    via('SPI_CLK',7.7,14.29),
    seg('SPI_CLK',7.7,14.29,2.0,14.29,.22,'B.Cu'),
    seg('SPI_CLK',2.0,14.29,2.0,43.0,.22,'B.Cu'),
    seg('SPI_CLK',2.0,43.0,4.5,43.0,.22,'B.Cu'),
    via('SPI_CLK',4.5,43.0),
    seg('SPI_CLK',4.5,43.0,24.0,43.0,.22,'F.Cu'),
    via('SPI_CLK',24.0,43.0),
    seg('SPI_CLK',24.0,43.0,35.2,36.135,.22,'B.Cu'),
    via('SPI_CLK',35.2,36.135),
    seg('SPI_CLK',35.2,36.135,33.7,36.135,.22,'F.Cu'),
]

# SPI_CLK FRAM -> microSD pad5.
r += [
    seg('SPI_CLK',35.2,36.135,38.5,36.135,.22,'F.Cu'),
    via('SPI_CLK',38.5,36.135),
    seg('SPI_CLK',38.5,36.135,38.5,40.0,.22,'B.Cu'),
    seg('SPI_CLK',38.5,40.0,47.295,40.0,.22,'B.Cu'),
    seg('SPI_CLK',47.295,40.0,47.295,37.8,.22,'B.Cu'),
    via('SPI_CLK',47.295,37.8),
    seg('SPI_CLK',47.295,37.8,47.295,36.55,.22,'F.Cu'),
]

# SPI_MOSI FRAM -> microSD pad3.
r += [
    seg('SPI_MOSI',36.2,37.405,40.0,33.6,.22,'B.Cu'),
    seg('SPI_MOSI',40.0,33.6,45.095,33.6,.22,'B.Cu'),
    via('SPI_MOSI',45.095,33.6),
    seg('SPI_MOSI',45.095,33.6,45.095,36.55,.22,'F.Cu'),
]

# SPI_MISO Core -> FRAM pad2. Use the empty left B.Cu corridor, then a F.Cu
# trunk at y=41 and a short B.Cu diagonal into the FRAM-side escape.
r += [
    seg('SPI_MISO',9.25,19.37,5.5,19.37,.22,'F.Cu'),
    via('SPI_MISO',5.5,19.37),
    seg('SPI_MISO',5.5,19.37,5.5,41.0,.22,'B.Cu'),
    via('SPI_MISO',5.5,41.0),
    seg('SPI_MISO',5.5,41.0,23.0,41.0,.22,'F.Cu'),
    via('SPI_MISO',23.0,41.0),
    seg('SPI_MISO',23.0,41.0,25.5,34.865,.22,'B.Cu'),
    via('SPI_MISO',25.5,34.865),
    seg('SPI_MISO',25.5,34.865,28.3,34.865,.22,'F.Cu'),
]

# SPI_MISO branch to microSD pad7. Continue the F.Cu trunk far enough right to
# clear the old MOSI diagonal, then use a lower B.Cu lane and return to F.Cu
# immediately below the microSD pad.
r += [
    seg('SPI_MISO',23.0,41.0,40.5,41.0,.22,'F.Cu'),
    via('SPI_MISO',40.5,41.0),
    seg('SPI_MISO',40.5,41.0,40.5,44.5,.22,'B.Cu'),
    seg('SPI_MISO',40.5,44.5,49.495,44.5,.22,'B.Cu'),
    seg('SPI_MISO',49.495,44.5,49.495,38.0,.22,'B.Cu'),
    via('SPI_MISO',49.495,38.0),
    seg('SPI_MISO',49.495,38.0,49.495,36.55,.22,'F.Cu'),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied final CAN + SPI storage post-pass to {P}')
