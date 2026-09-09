from pathlib import Path

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Remaining 3V3_MAIN loads. Keep each plane entry independent so no surface
# 3V3 trunk crosses watchdog, FB or ACC signal routing.
def seg(x1, y1, x2, y2, w=.28):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "F.Cu") (net "3V3_MAIN"))')

def via(x, y, size=.70, drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "3V3_MAIN"))')

r = [
    # TP1: local plane entry above the lower-edge CAN2_RX B.Cu corridor.
    seg(8.00, 61.00, 8.00, 59.80, .32),
    via(8.00, 59.80),

    # Q4 source (watchdog 3V3): escape west, away from WDT_3V3 and FRAM_CS.
    seg(20.95, 30.55, 19.55, 30.55, .32),
    via(19.55, 30.55),

    # R6 upper leg of FB divider: west entry keeps clear of FB_3V3 to the right.
    seg(52.50, 51.50, 51.20, 51.50),
    via(51.20, 51.50),

    # R12 ACC pull-up: short west entry, clear of ACC_BASE below.
    seg(83.50, 34.00, 82.20, 34.00),
    via(82.20, 34.00),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied remaining 3V3_MAIN fanout to {P}')
