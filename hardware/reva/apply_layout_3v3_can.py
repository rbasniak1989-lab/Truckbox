from pathlib import Path

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

def seg(x1, y1, x2, y2, w=.28):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "F.Cu") (net "3V3_MAIN"))')

def via(x, y, size=.70, drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "3V3_MAIN"))')

r = [
    # CAN1 TXD pull-up R3
    seg(64.50, 39.00, 63.20, 39.00),
    via(63.20, 39.00),

    # CAN1 decoupling C11: move plane entry away from CAN2_RX via.
    seg(66.00, 41.00, 66.00, 42.20, .32),
    seg(66.00, 42.20, 66.70, 42.20, .32),
    via(66.70, 42.20),

    # U3 VCC
    seg(69.30, 43.135, 68.00, 43.135, .32),
    via(68.00, 43.135),

    # CAN2 R4 + C12 share the same 3V3 surface link. Enter the internal plane
    # sideways at y=52 instead of below C12, keeping clear of buck input C1.
    seg(63.00, 51.00, 63.00, 53.00, .28),
    seg(63.00, 52.00, 62.80, 52.00, .32),
    via(62.80, 52.00),

    # U4 VCC: shift plane-entry via upward/right to clear C2 GND pad.
    seg(69.30, 49.135, 68.00, 48.90, .32),
    via(68.00, 48.90),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied CAN 3V3_MAIN fanout to {P}')
