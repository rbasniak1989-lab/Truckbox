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
    seg(64.50, 39.00, 63.20, 39.00),
    via(63.20, 39.00),
    seg(66.00, 41.00, 66.20, 42.00, .32),
    via(66.20, 42.00),
    seg(69.30, 43.135, 68.00, 43.135, .32),
    via(68.00, 43.135),
    seg(63.00, 51.00, 63.00, 53.00, .28),
    seg(63.00, 53.00, 63.00, 54.20, .32),
    via(63.00, 54.20),
    seg(69.30, 49.135, 67.80, 49.135, .32),
    via(67.80, 49.135),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied CAN 3V3_MAIN fanout to {P}')
