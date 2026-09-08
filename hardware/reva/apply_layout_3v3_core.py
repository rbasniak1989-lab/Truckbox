from pathlib import Path

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Core-side 3V3_MAIN fanout. The full In2.Cu 3V3_MAIN plane already exists.
# Use independent short pad-to-plane escapes so no 3V3 trace crosses the local
# SPI corridor and no trace passes through the GND-side pads of the decouplers.
# Endpoints verified from the clean pre-run94 board:
# U1.2=(9.25,9.21), C9.1=(5.0,9.0), C15.1=(4.9,12.0), R13.1=(5.0,15.0).

def seg(x1,y1,x2,y2,w=.28,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "3V3_MAIN"))')

def via(x,y,size=.70,drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "3V3_MAIN"))')

r=[
    # C9: escape west, away from pad 2 (GND) on the east side.
    seg(5.00,9.00,3.60,9.00,.28),
    via(3.60,9.00),

    # C15: independent plane entry; no vertical spine through SPI_MOSI.
    seg(4.90,12.00,3.60,12.00,.32),
    via(3.60,12.00),

    # R13 pull-up: independent plane entry below the SPI corridor.
    seg(5.00,15.00,3.60,15.00,.28),
    via(3.60,15.00),

    # U1 supply: short escape to a via just outside the ESP32 courtyard/body.
    seg(9.25,9.21,7.80,9.21,.32),
    via(7.80,9.21),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied corrected Core 3V3_MAIN local fanout to {P}')
