from pathlib import Path

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# 3V3_MAIN fanout for Link/GNSS switched-rail source side.
# Each load enters the internal 3V3 plane locally, keeping surface copper away
# from LINK_PWR_N, SD_PWR_N, 3V3_LINK and 3V3_SD trunks.
def seg(x1, y1, x2, y2, w=.28):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "F.Cu") (net "3V3_MAIN"))')

def via(x, y, size=.70, drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "3V3_MAIN"))')

r = [
    # Q1 source: escape upward, away from LINK_PWR_N and 3V3_LINK.
    seg(65.95, 6.55, 65.95, 5.30, .32),
    via(65.95, 5.30),

    # R26 source-side pull-up: short west escape; plane entry remains clear of
    # the diagonal 3V3_LINK route on B.Cu.
    seg(63.50, 9.00, 62.20, 9.00),
    via(62.20, 9.00),

    # GNSS VBAT (U5 pad 6): right-side local escape, between GNSS_ON and
    # 3V3_LINK corridors.
    seg(54.85, 15.90, 56.20, 15.90, .32),
    via(56.20, 15.90),

    # Q2 source: short east/up escape, above the 3V3_SD B.Cu trunk and clear of
    # SD_PWR_N on F.Cu.
    seg(54.45, 32.05, 55.20, 31.90, .32),
    via(55.20, 31.90),

    # R27 source-side pull-up: west escape, clear of SD_PWR_N vertical routing.
    seg(54.50, 30.00, 53.00, 30.00),
    via(53.00, 30.00),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied Link/GNSS 3V3_MAIN fanout to {P}')
