from pathlib import Path

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Core-side 3V3_MAIN fanout. The full In2.Cu 3V3_MAIN plane already exists.
# Keep copper outside the ESP32 body where possible and use one local plane via.
# Endpoints from the clean run92 board:
# U1.2=(9.25,9.21), C9.1=(5.0,9.0), C15.1=(4.9,12.0), R13.1=(5.0,15.0).

def seg(x1,y1,x2,y2,w=.28,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "3V3_MAIN"))')

def via(x,y,size=.70,drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "3V3_MAIN"))')

r=[
    # Decoupling/pull-up spine along the left edge, clear of SPI_MOSI at x=3.5.
    seg(5.00,9.00,5.00,11.90,.28),
    seg(5.00,11.90,4.90,12.00,.28),
    seg(4.90,12.00,5.00,12.10,.28),
    seg(5.00,12.10,5.00,15.00,.28),

    # U1 supply joins the same node without running under the module.
    seg(5.00,9.00,7.80,9.00,.32),
    seg(7.80,9.00,9.25,9.21,.32),

    # Plane entry remains left of the ESP32 module body/edge pads.
    via(7.80,9.00,.70,.35),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Core 3V3_MAIN local fanout to {P}')
