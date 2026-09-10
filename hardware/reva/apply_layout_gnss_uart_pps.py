from pathlib import Path

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Final GNSS RX/TX/PPS routing.
# The long runs use In2.Cu (POWER_SIGNALS), which was reserved for 3V3_MAIN
# plus slow signals. All three lanes stay below the ESP32-Link module body and
# outside its RF area. Right-side pin escapes are kept on F/B copper so the
# two already-frozen Core<->Link UART verticals on In2.Cu remain untouched.
def seg(net, layer, x1, y1, x2, y2, w=.20):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "{net}"))')

def via(net, x, y, size=.55, drill=.30):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "{net}"))')

r = [
    # --- U5 source fanout: short, straight F.Cu escapes ---
    seg('GNSS_PPS', 'F.Cu', 54.85, 18.10, 56.10, 18.10),
    via('GNSS_PPS', 56.10, 18.10),
    seg('GNSS_TX', 'F.Cu', 54.85, 19.20, 56.10, 19.20),
    via('GNSS_TX', 56.10, 19.20),
    seg('GNSS_RX', 'F.Cu', 54.85, 20.30, 56.10, 20.30),
    via('GNSS_RX', 56.10, 20.30),

    # --- In2.Cu stair-step fanout and three separated long lanes ---
    # PPS takes the upper lane. Its x=63 drop occurs to the right of the
    # source fanout, so it cannot pinch the TX/RX source vias.
    seg('GNSS_PPS', 'In2.Cu', 56.10, 18.10, 63.00, 18.10),
    seg('GNSS_PPS', 'In2.Cu', 63.00, 18.10, 63.00, 25.80),
    seg('GNSS_PPS', 'In2.Cu', 63.00, 25.80, 88.00, 25.80),
    via('GNSS_PPS', 88.00, 25.80),

    # TX middle lane. x=61.8 stays left of the PPS vertical and clears the
    # existing GNSS_ON via at (61,20).
    seg('GNSS_TX', 'In2.Cu', 56.10, 19.20, 61.80, 19.20),
    seg('GNSS_TX', 'In2.Cu', 61.80, 19.20, 61.80, 27.80),
    seg('GNSS_TX', 'In2.Cu', 61.80, 27.80, 88.00, 27.80),
    via('GNSS_TX', 88.00, 27.80),

    # RX lower lane. The endpoint is offset to x=87 so its via retains full
    # clearance from the TX via while staying above the frozen UART y=29.1.
    seg('GNSS_RX', 'In2.Cu', 56.10, 20.30, 60.20, 20.30),
    seg('GNSS_RX', 'In2.Cu', 60.20, 20.30, 60.20, 28.40),
    seg('GNSS_RX', 'In2.Cu', 60.20, 28.40, 87.00, 28.40),
    via('GNSS_RX', 87.00, 28.40),

    # --- PPS -> U2 pin 16 ---
    # Remain on F.Cu around the lower/right module perimeter. x=92 is clear of
    # the LINK_BOOT diagonal/via and of the 3V3_LINK via at (93.5,22.8).
    seg('GNSS_PPS', 'F.Cu', 88.00, 25.80, 89.00, 25.80),
    seg('GNSS_PPS', 'F.Cu', 89.00, 25.80, 89.00, 26.80),
    seg('GNSS_PPS', 'F.Cu', 89.00, 26.80, 92.00, 26.80),
    seg('GNSS_PPS', 'F.Cu', 92.00, 26.80, 92.00, 23.18),
    seg('GNSS_PPS', 'F.Cu', 92.00, 23.18, 90.75, 23.18),

    # --- TX -> U2 pin 25 ---
    # Cross the LINK_BOOT y=26 B.Cu branch by changing to B.Cu above it.
    # x=92.7 sits between the frozen In2 UART at x=92 and 3V3_LINK at x=93.5.
    seg('GNSS_TX', 'F.Cu', 88.00, 27.80, 92.70, 27.80),
    seg('GNSS_TX', 'F.Cu', 92.70, 27.80, 92.70, 25.30),
    via('GNSS_TX', 92.70, 25.30),
    seg('GNSS_TX', 'B.Cu', 92.70, 25.30, 92.70, 11.75),
    via('GNSS_TX', 92.70, 11.75),
    seg('GNSS_TX', 'F.Cu', 92.70, 11.75, 90.75, 11.75),

    # --- RX -> U2 pin 24 ---
    # Outer x=95.2 B.Cu lane clears both LINK_BOOT at x=96 and the frozen
    # Core->Link UART at x=94.5 on In2.Cu.
    seg('GNSS_RX', 'F.Cu', 87.00, 28.40, 95.20, 28.40),
    seg('GNSS_RX', 'F.Cu', 95.20, 28.40, 95.20, 25.30),
    via('GNSS_RX', 95.20, 25.30),
    seg('GNSS_RX', 'B.Cu', 95.20, 25.30, 95.20, 13.02),
    via('GNSS_RX', 95.20, 13.02),
    seg('GNSS_RX', 'F.Cu', 95.20, 13.02, 90.75, 13.02),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied final GNSS UART/PPS routing pass to {P}')
