from pathlib import Path

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# 3V3_MAIN fanout for the FRAM / local pull-up cluster.
# The internal In2.Cu 3V3_MAIN plane already spans the board, so each load gets
# a short independent F.Cu escape into a through-via. This avoids building a
# surface 3V3 spine through the already-routed SPI / CAN / FRAM signal corridor.

def seg(x1, y1, x2, y2, w=.28):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "F.Cu") (net "3V3_MAIN"))')


def via(x, y, size=.70, drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "3V3_MAIN"))')

r = [
    # R24 / FRAM_CS pull-up. Drop south; keeps clearance from FRAM_WP at x=26.6
    # and from the FRAM_CS diagonal immediately east of this pad.
    seg(27.50, 30.00, 27.50, 31.00),
    via(27.50, 31.00),

    # R39 / FRAM_WP pull-up.
    seg(30.50, 30.00, 30.50, 31.00),
    via(30.50, 31.00),

    # R40 / FRAM_HOLD pull-up.
    seg(33.50, 30.00, 33.50, 31.00),
    via(33.50, 31.00),

    # U7 VDD (pad 8). Escape east into the plane, between the HOLD routing and
    # the SOIC body without touching pad 7.
    seg(33.70, 33.595, 34.90, 33.595, .32),
    via(34.90, 33.595),

    # C13 FRAM local decoupling. Pad 2 is GND on the east side, so escape west.
    # The via remains clear of SPI_MOSI on B.Cu and SPI_MISO at y=41 on F.Cu.
    seg(35.50, 40.00, 34.30, 40.00, .32),
    via(34.30, 40.00),

    # R29 / CORE_BOOT pull-up. Route down-left, clear of the CORE_BOOT via and
    # diagonal at y~24.5-26.2.
    seg(29.50, 25.00, 28.00, 26.00),
    via(28.00, 26.00),

    # R20 / CAN_MODE default-OFF pull-up. A via at y=21 would hit CAN2_RX on
    # B.Cu, so first escape vertically to y=22.2 before entering the plane.
    seg(29.50, 21.00, 29.50, 22.20),
    via(29.50, 22.20),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied FRAM/pull-up 3V3_MAIN fanout to {P}')
