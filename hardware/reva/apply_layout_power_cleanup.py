from pathlib import Path

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

def seg(net, layer, x1, y1, x2, y2, w=.25):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "{net}"))')

def via(net, x, y, size=.70, drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "{net}"))')

r = [
    # TPS54260 pad 9 GND -> exposed PowerPAD GND. Keep the link horizontal
    # inside the pad-9 corridor so it does not approach COMP (pad 8) or SW.
    seg('GND', 'F.Cu', 55.85, 58.50, 57.25, 58.50, .20),

    # R31 VIN_PROT branch. Stay on the clear B.Cu y=45 corridor all the way
    # outside the CAN/buck cluster. Drop at x=70, just beyond the CAN2_RX
    # horizontal endpoint, then make only a short F.Cu hop into the existing
    # VIN_PROT endpoint. This avoids COMP, BUCK_EN, the 3V3 via and C1.
    seg('VIN_PROT', 'F.Cu', 54.50, 47.00, 53.30, 47.00, .25),
    via('VIN_PROT', 53.30, 47.00),
    seg('VIN_PROT', 'B.Cu', 53.30, 47.00, 53.30, 45.00, .25),
    seg('VIN_PROT', 'B.Cu', 53.30, 45.00, 70.00, 45.00, .25),
    seg('VIN_PROT', 'B.Cu', 70.00, 45.00, 70.00, 53.50, .25),
    via('VIN_PROT', 70.00, 53.50),
    seg('VIN_PROT', 'F.Cu', 70.00, 53.50, 69.50, 53.975, .25),

    # Three isolated F.Cu GND islands identified from the filled-zone
    # polygons in run 110. Stitch each directly to the continuous inner GND
    # plane. Locations are inside the copper islands and clear of B.Cu routes.
    via('GND', 91.29, 49.25, .70, .35),
    via('GND', 91.68, 44.25, .70, .35),
    via('GND', 7.21, 16.81, .70, .35),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied final power/GND cleanup to {P}')
