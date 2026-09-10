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
    # ACC_N U1 -> Q4. Keep this branch on F.Cu; it leaves U1 pad 8 through
    # the free non-antenna side of the module and drops into the watchdog area.
    # No via is placed below either ESP32 module.
    seg('ACC_N', 'F.Cu', 9.25, 16.83, 10.50, 17.25),
    seg('ACC_N', 'F.Cu', 10.50, 17.25, 16.25, 19.25),
    seg('ACC_N', 'F.Cu', 16.25, 19.25, 23.25, 26.00),
    seg('ACC_N', 'F.Cu', 23.25, 26.00, 23.00, 28.25),
    seg('ACC_N', 'F.Cu', 23.00, 28.25, 21.25, 32.25),
    seg('ACC_N', 'F.Cu', 21.25, 32.25, 20.95, 32.45),

    # Q4 -> ACC input/filter cluster. Route across the open F.Cu corridor,
    # use a short B.Cu bridge only through the crowded center, then return to
    # F.Cu for the long slow-signal run. This avoids cutting the 3V3_MAIN
    # internal plane.
    seg('ACC_N', 'F.Cu', 20.95, 32.45, 22.25, 30.75),
    seg('ACC_N', 'F.Cu', 22.25, 30.75, 27.75, 29.00),
    seg('ACC_N', 'F.Cu', 27.75, 29.00, 33.25, 28.25),
    seg('ACC_N', 'F.Cu', 33.25, 28.25, 37.50, 28.25),
    via('ACC_N', 37.50, 28.25),
    seg('ACC_N', 'B.Cu', 37.50, 28.25, 49.00, 33.00),
    via('ACC_N', 49.00, 33.00),
    seg('ACC_N', 'F.Cu', 49.00, 33.00, 54.00, 34.75),
    seg('ACC_N', 'F.Cu', 54.00, 34.75, 83.50, 40.00),

    # Local ACC_N filter / transistor connections on the right side.
    seg('ACC_N', 'F.Cu', 83.50, 40.00, 82.75, 37.25),
    seg('ACC_N', 'F.Cu', 82.75, 37.25, 82.75, 36.25),
    seg('ACC_N', 'F.Cu', 82.75, 36.25, 84.25, 34.50),
    seg('ACC_N', 'F.Cu', 84.25, 34.50, 84.50, 34.00),
    seg('ACC_N', 'F.Cu', 84.50, 34.00, 88.00, 35.50),
    seg('ACC_N', 'F.Cu', 88.00, 35.50, 89.05, 37.00),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied ACC_N routing pass to {P}')
