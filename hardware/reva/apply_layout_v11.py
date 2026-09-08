from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')
net_id={name:int(i) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)}

# Rev.A v11 - local low-speed routing.
# Keep critical power/CAN/RF geometry untouched; close only short, local nets.

# Board rule minimum is 0.20 mm. v10 used 0.18 mm only for the PH pin escape.
s=s.replace('(width 0.180) (layer "F.Cu") (net %d)' % net_id['SW_NODE'],
            '(width 0.200) (layer "F.Cu") (net %d)' % net_id['SW_NODE'])


def seg(net,x1,y1,x2,y2,w=.25,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {net_id[net]}))')

r=[]

# Core boot pull-up.
r += [
    seg('CORE_BOOT',26.75,24.45,28.5,24.45),
    seg('CORE_BOOT',28.5,24.45,30.5,25.0),
]

# TCAN TXD safety pull-ups: physical TXD remains disconnected from MCU.
r += [
    seg('CAN1_TX_SAFE',67.0,44.0,67.4,42.4),
    seg('CAN1_TX_SAFE',67.4,42.4,69.3,40.595),
    seg('CAN2_TX_SAFE',64.0,51.0,65.2,49.3),
    seg('CAN2_TX_SAFE',65.2,49.3,69.3,46.595),
]

# FRAM static control pins.
r += [
    seg('FRAM_WP',31.5,30.0,30.2,32.0),
    seg('FRAM_WP',30.2,32.0,28.3,36.135),
    seg('FRAM_HOLD',34.5,30.0,34.5,33.2),
    seg('FRAM_HOLD',34.5,33.2,33.7,34.865),
]

# Watchdog timing resistor.
r += [
    seg('WDT_REXT',17.5,39.0,18.5,37.8),
    seg('WDT_REXT',18.5,37.8,20.9,35.05),
]

# ACC divider/base local network.
r += [
    seg('ACC_MID1',92.5,35.0,92.1,36.0),
    seg('ACC_MID1',92.1,36.0,91.5,37.0),
    seg('ACC_BASE',92.5,37.0,90.2,38.2),
    seg('ACC_BASE',90.2,38.2,86.95,37.95),
    seg('ACC_BASE',86.95,37.95,85.3,37.0),
    seg('ACC_BASE',85.3,37.0,83.5,37.0),
    seg('ACC_BASE',86.95,37.95,87.0,39.4),
    seg('ACC_BASE',87.0,39.4,88.05,40.0),
]

# Link enable RC and boot/service switch.
r += [
    seg('LINK_EN',73.25,10.48,71.5,12.5),
    seg('LINK_EN',71.5,12.5,69.5,16.0),
    seg('LINK_EN',69.5,16.0,69.5,17.8),
    seg('LINK_EN',69.5,17.8,68.5,19.0),
    seg('LINK_BOOT',90.75,24.45,92.5,24.45),
    seg('LINK_BOOT',92.5,24.45,94.5,24.5),
    seg('LINK_BOOT',94.5,24.5,95.6,23.0),
    seg('LINK_BOOT',95.6,23.0,94.5,20.5),
]

# GNSS ON/OFF pull-up/control.
r += [
    seg('GNSS_ON',54.85,17.0,56.0,18.6),
    seg('GNSS_ON',56.0,18.6,59.5,20.0),
]

# USB service pads. Route around the series resistors; never bypass them.
r += [
    seg('USB_D-_LINK_MCU',73.25,23.18,70.5,23.18,.22),
    seg('USB_D-_LINK_MCU',70.5,23.18,67.5,23.0,.22),
    seg('USB_D+_LINK_MCU',73.25,24.45,70.5,24.45,.22),
    seg('USB_D+_LINK_MCU',70.5,24.45,67.5,25.0,.22),
    seg('USB_D-_LINK',68.5,23.0,68.5,21.8,.22),
    seg('USB_D-_LINK',68.5,21.8,65.2,21.8,.22),
    seg('USB_D-_LINK',65.2,21.8,64.0,23.0,.22),
    seg('USB_D+_LINK',68.5,25.0,68.5,26.2,.22),
    seg('USB_D+_LINK',68.5,26.2,65.2,26.2,.22),
    seg('USB_D+_LINK',65.2,26.2,64.0,25.0,.22),
]

# Local buck/passive interconnects only; sensitive long returns remain for later.
r += [
    seg('BUCK_EN',55.5,47.0,57.5,47.0,.22),
    seg('COMP_RC',61.5,47.0,63.5,47.0,.22),
    seg('FB_3V3',53.5,51.5,52.5,53.5,.22),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v11 local routing to {P}')
