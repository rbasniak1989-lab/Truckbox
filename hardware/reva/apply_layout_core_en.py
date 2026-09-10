from pathlib import Path

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')


def seg(net, layer, x1, y1, x2, y2, w=.22):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "{net}"))')


def via(net, x, y, size=.55, drill=.30):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "{net}"))')


r = [
    # CORE_EN: U1 pin 3 -> R13. Exit the module pad on F.Cu, place the first
    # via outside the U1 courtyard, and step around the existing SPI_CLK via
    # on B.Cu. No via is placed beneath the ESP32 module.
    seg('CORE_EN', 'F.Cu', 9.25, 10.48, 7.45, 10.48),
    via('CORE_EN', 7.45, 10.48),
    seg('CORE_EN', 'B.Cu', 7.45, 10.48, 7.45, 13.40),
    seg('CORE_EN', 'B.Cu', 7.45, 13.40, 8.60, 13.40),
    seg('CORE_EN', 'B.Cu', 8.60, 13.40, 8.60, 15.20),
    seg('CORE_EN', 'B.Cu', 8.60, 15.20, 6.20, 16.00),
    via('CORE_EN', 6.20, 16.00),
    seg('CORE_EN', 'F.Cu', 6.20, 16.00, 6.00, 15.00),

    # R13 -> C19. Dogleg east/south/west to clear the existing 3V3_MAIN via
    # at (5,16) and the SPI_MOSI escape at x=7.
    seg('CORE_EN', 'F.Cu', 6.00, 15.00, 6.45, 15.00),
    seg('CORE_EN', 'F.Cu', 6.45, 15.00, 6.45, 17.10),
    seg('CORE_EN', 'F.Cu', 6.45, 17.10, 4.50, 17.10),
    seg('CORE_EN', 'F.Cu', 4.50, 17.10, 5.00, 18.00),

    # C19 -> external trunk. Cross the SPI_MOSI/SPI_CLK fence through the
    # narrow but DRC-safe corridor between x=2 and x=3.5 using 0.55/0.30 vias,
    # then run down the clear F.Cu edge corridor.
    seg('CORE_EN', 'F.Cu', 5.00, 18.00, 4.50, 18.80),
    via('CORE_EN', 4.50, 18.80),
    seg('CORE_EN', 'B.Cu', 4.50, 18.80, 2.75, 18.80),
    via('CORE_EN', 2.75, 18.80),
    seg('CORE_EN', 'F.Cu', 2.75, 18.80, 1.20, 18.80),
    seg('CORE_EN', 'F.Cu', 1.20, 18.80, 1.20, 57.00),

    # Branch to watchdog U8 pin 6. Hop to B.Cu only to cross the vertical
    # SPI_MOSI fence, then return to the open F.Cu corridor below U8.
    seg('CORE_EN', 'F.Cu', 1.20, 38.00, 2.75, 38.00),
    via('CORE_EN', 2.75, 38.00),
    seg('CORE_EN', 'B.Cu', 2.75, 38.00, 4.25, 38.00),
    via('CORE_EN', 4.25, 38.00),
    seg('CORE_EN', 'F.Cu', 4.25, 38.00, 24.50, 38.00),
    seg('CORE_EN', 'F.Cu', 24.50, 38.00, 24.50, 36.95),
    seg('CORE_EN', 'F.Cu', 24.50, 36.95, 23.10, 36.95),

    # Branch to TP5. The y=57 corridor is below the SPI storage routes and
    # above the CAN1/CAN2 bottom escapes, so it stays entirely on F.Cu.
    seg('CORE_EN', 'F.Cu', 1.20, 57.00, 20.00, 57.00),
    seg('CORE_EN', 'F.Cu', 20.00, 57.00, 20.00, 61.00),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied CORE_EN routing pass to {P}')
