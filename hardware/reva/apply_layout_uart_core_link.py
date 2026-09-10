from pathlib import Path

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Final Core <-> Link UART routing.
# In2.Cu (POWER_SIGNALS) is intentionally reserved for 3V3_MAIN plus slow
# signals. Using it here avoids the dense CAN/USB corridors on F.Cu/B.Cu and
# keeps all UART copper below the ESP32 module outlines / RF areas.
def seg(net, layer, x1, y1, x2, y2, w=.22):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "{net}"))')

def via(net, x, y, size=.55, drill=.30):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "{net}"))')

r = [
    # CORE_TO_LINK_MCU: U1 GPIO22 -> R33 pad 1.
    seg('CORE_TO_LINK_MCU', 'F.Cu', 26.75, 18.10, 28.00, 18.10),
    via('CORE_TO_LINK_MCU', 28.00, 18.10),
    seg('CORE_TO_LINK_MCU', 'In2.Cu', 28.00, 18.10, 31.00, 18.10),
    seg('CORE_TO_LINK_MCU', 'In2.Cu', 31.00, 18.10, 36.50, 24.50),
    seg('CORE_TO_LINK_MCU', 'In2.Cu', 36.50, 24.50, 38.50, 27.00),
    via('CORE_TO_LINK_MCU', 38.50, 27.00),
    seg('CORE_TO_LINK_MCU', 'F.Cu', 38.50, 27.00, 38.50, 28.00),

    # LINK_TO_CORE: U1 GPIO23 <- R34 pad 2. Keep this route above the first
    # UART until it has passed the R33 endpoint, then descend from the right.
    seg('LINK_TO_CORE', 'F.Cu', 26.75, 16.83, 28.00, 16.83),
    via('LINK_TO_CORE', 28.00, 16.83),
    seg('LINK_TO_CORE', 'In2.Cu', 28.00, 16.83, 33.00, 16.83),
    seg('LINK_TO_CORE', 'In2.Cu', 33.00, 16.83, 41.00, 22.00),
    seg('LINK_TO_CORE', 'In2.Cu', 41.00, 22.00, 45.50, 22.00),
    seg('LINK_TO_CORE', 'In2.Cu', 45.50, 22.00, 45.50, 27.00),
    seg('LINK_TO_CORE', 'In2.Cu', 45.50, 27.00, 44.50, 28.00),
    via('LINK_TO_CORE', 44.50, 28.00),
    seg('LINK_TO_CORE', 'F.Cu', 44.50, 28.00, 43.50, 28.00),

    # LINK_TO_CORE_MCU: R34 pad 1 -> U2 GPIO22. Main internal lane y=29.1.
    # x=92 rises outside the Link module body and stays clear of LINK_BOOT.
    seg('LINK_TO_CORE_MCU', 'F.Cu', 42.50, 28.00, 42.50, 26.00),
    via('LINK_TO_CORE_MCU', 42.50, 26.00),
    seg('LINK_TO_CORE_MCU', 'In2.Cu', 42.50, 26.00, 42.50, 29.10),
    seg('LINK_TO_CORE_MCU', 'In2.Cu', 42.50, 29.10, 92.00, 29.10),
    seg('LINK_TO_CORE_MCU', 'In2.Cu', 92.00, 29.10, 92.00, 18.10),
    via('LINK_TO_CORE_MCU', 92.00, 18.10),
    seg('LINK_TO_CORE_MCU', 'F.Cu', 92.00, 18.10, 90.75, 18.10),

    # CORE_TO_LINK: R33 pad 2 -> U2 GPIO23. Lower internal lane y=31.0 then
    # wrap outside the other UART at x=94.5 before returning to U2 pin 21.
    seg('CORE_TO_LINK', 'F.Cu', 39.50, 28.00, 39.50, 30.00),
    via('CORE_TO_LINK', 39.50, 30.00),
    seg('CORE_TO_LINK', 'In2.Cu', 39.50, 30.00, 40.50, 31.00),
    seg('CORE_TO_LINK', 'In2.Cu', 40.50, 31.00, 94.50, 31.00),
    seg('CORE_TO_LINK', 'In2.Cu', 94.50, 31.00, 94.50, 16.83),
    seg('CORE_TO_LINK', 'In2.Cu', 94.50, 16.83, 92.00, 16.83),
    via('CORE_TO_LINK', 92.00, 16.83),
    seg('CORE_TO_LINK', 'F.Cu', 92.00, 16.83, 90.75, 16.83),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied Core/Link UART routing pass to {P}')
