from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Production USB-C service rebuild, based on the official KiCad/GCT USB4105
# footprint geometry. This pass starts from the clean run-127 topology.
#
# Mechanical reference (GCT USB4105):
#   PCB edge: local y = +3.675 mm
#   SMT row:  local y = -3.680 mm
#   body: 8.94 x 7.35 mm
# With J3 at (96.325, 28.600), +90 deg, the PCB-edge line is x=100.000.
#
# This validation pass intentionally adds only J3 + USB D+/D- and the local
# route clearances needed around it. CC pull-downs and the ESD array are added
# in the next pass after the connector mechanics are proven clean.


def balanced_block(text, start):
    depth = 0
    in_q = False
    esc = False
    for j in range(start, len(text)):
        c = text[j]
        if in_q:
            if esc:
                esc = False
            elif c == '\\':
                esc = True
            elif c == '"':
                in_q = False
            continue
        if c == '"':
            in_q = True
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return j + 1
    raise RuntimeError('unterminated s-expression')


def ref_marker_pos(text, ref):
    markers = (
        f'(property "Reference" "{ref}"',
        f'(fp_text reference "{ref}"',
    )
    hits = [text.find(m) for m in markers if text.find(m) >= 0]
    return min(hits) if hits else -1


def remove_ref(text, ref):
    while True:
        m = ref_marker_pos(text, ref)
        if m < 0:
            return text
        i = text.rfind('(footprint ', 0, m)
        if i < 0:
            raise RuntimeError(f'footprint start not found for {ref}')
        j = balanced_block(text, i)
        text = text[:i] + text[j:]


def move_ref(text, ref, x, y, rot=0):
    m = ref_marker_pos(text, ref)
    if m < 0:
        raise RuntimeError(f'{ref} not found')
    i = text.rfind('(footprint ', 0, m)
    j = balanced_block(text, i)
    b = text[i:j]
    relm = m - i
    pre = b[:relm]
    post = b[relm:]
    repl = f'(at {x:g} {y:g}' + (f' {rot:g}' if rot else '') + ')'
    pre2, n = re.subn(r'\(at\s+[-+0-9.]+\s+[-+0-9.]+(?:\s+[-+0-9.]+)?\)', repl, pre, count=1)
    if n != 1:
        raise RuntimeError(f'placement not replaced for {ref}')
    return text[:i] + pre2 + post + text[j:]


def block_net(b):
    m = re.search(r'\(net(?:\s+\d+)?\s+"([^"]+)"\)', b)
    return m.group(1) if m else None


def block_layer(b):
    m = re.search(r'\(layer\s+"([^"]+)"\)', b)
    return m.group(1) if m else None


def block_points(b):
    pts = []
    for m in re.finditer(r'\((?:start|end|at)\s+([-+0-9.]+)\s+([-+0-9.]+)', b):
        pts.append((float(m.group(1)), float(m.group(2))))
    return pts


def remove_blocks(text, token, pred):
    ranges = []
    pos = 0
    needle = '(' + token
    while True:
        i = text.find(needle, pos)
        if i < 0:
            break
        j = balanced_block(text, i)
        b = text[i:j]
        if pred(b):
            ranges.append((i, j))
        pos = j
    for i, j in reversed(ranges):
        text = text[:i] + text[j:]
    return text


def has_point(b, x, y, tol=.01):
    return any(abs(px-x) <= tol and abs(py-y) <= tol for px, py in block_points(b))


# Old production-test parts must not survive if this pass is rerun.
for ref in ('J3', 'D6', 'R1', 'R2'):
    s = remove_ref(s, ref)

# Move the Link BOOT pull-up out of the connector body/courtyard. 180 degrees
# places 3V3_LINK on the right pad and LINK_BOOT on the left pad.
s = move_ref(s, 'R30', 92.5, 21.3, 180)

# Remove the old LINK_BOOT route completely and rebuild it on F.Cu above J3.
s = remove_blocks(s, 'segment', lambda b: block_net(b) == 'LINK_BOOT')
s = remove_blocks(s, 'via', lambda b: block_net(b) == 'LINK_BOOT')

# Remove only the old R30 3V3_LINK feed, preserving the frozen main trunk/via.
s = remove_blocks(
    s, 'segment',
    lambda b: block_net(b) == '3V3_LINK' and block_layer(b) == 'F.Cu'
    and has_point(b, 93.5, 24.5) and has_point(b, 93.5, 22.8)
)

# Right-side GNSS escapes intersect the official connector row. Keep all
# source fanout + In2 long lanes, remove only F/B copper and vias at x>=87.
gnss = {'GNSS_RX', 'GNSS_TX', 'GNSS_PPS'}
s = remove_blocks(
    s, 'segment',
    lambda b: block_net(b) in gnss and block_layer(b) in ('F.Cu', 'B.Cu')
    and block_points(b) and min(x for x, _ in block_points(b)) >= 87.0
)
s = remove_blocks(
    s, 'via',
    lambda b: block_net(b) in gnss and block_points(b)
    and block_points(b)[0][0] >= 87.0
)

# ACC_RAW previously occupied y=33 directly through J3's lower shell stake.
# Rebuild the whole net locally on B.Cu below the connector.
s = remove_blocks(s, 'segment', lambda b: block_net(b) == 'ACC_RAW')
s = remove_blocks(s, 'via', lambda b: block_net(b) == 'ACC_RAW')

# This old GND stitch sits directly in the new USB pair corridor.
s = remove_blocks(
    s, 'via',
    lambda b: block_net(b) == 'GND' and has_point(b, 90.0, 30.0)
)


def seg(net, layer, x1, y1, x2, y2, w=.22):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "{net}"))')


def via(net, x, y, size=.55, drill=.30):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "{net}"))')


def fp_begin(name, ref, value, x, y, rot=0):
    rr = f' {rot:g}' if rot else ''
    return [
        f'  (footprint "TruckBox:{name}" (layer "F.Cu")',
        f'    (at {x:g} {y:g}{rr})',
        '    (attr smd)',
        f'    (fp_text reference "{ref}" (at 0 -5.5) (layer "F.SilkS") hide (effects (font (size 0.8 0.8) (thickness 0.12))))',
        f'    (fp_text value "{value}" (at 0 5) (layer "F.Fab") hide (effects (font (size 0.7 0.7) (thickness 0.1))))',
    ]


def smd_pad(num, x, y, sx, sy, net=None):
    nn = f' (net "{net}")' if net else ''
    return (f'    (pad "{num}" smd roundrect (at {x:g} {y:g}) (size {sx:g} {sy:g}) '
            f'(layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25){nn})')


def j3_usb4105():
    # Exact solder/mechanical coordinates from KiCad 10 official footprint and
    # GCT recommended PCB layout. Electrically duplicate lands are represented
    # once because USB4105 exposes 12 physical PCB solder lands.
    z = fp_begin('USB_C_Receptacle_GCT_USB4105_16P', 'J3', 'USB4105-GF-A-060', 96.325, 28.6, 90)
    z += [
        '    (fp_line (start 5 3.675) (end -5 3.675) (stroke (width 0.1) (type solid)) (layer "Dwgs.User"))',
        '    (fp_rect (start -5.32 -4.76) (end 5.32 4.18) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))',
        '    (fp_rect (start -4.47 -3.675) (end 4.47 3.675) (stroke (width 0.10) (type solid)) (fill none) (layer "F.Fab"))',
        # 12 physical solder lands. VBUS and SBU are intentionally NC.
        smd_pad('1',  -3.20, -3.68, .60, 1.15, 'GND'),
        smd_pad('2',  -2.40, -3.68, .60, 1.15, None),
        smd_pad('3',  -1.75, -3.68, .30, 1.15, None),
        smd_pad('4',  -1.25, -3.68, .30, 1.15, 'USB_CC1'),
        smd_pad('5',  -0.75, -3.68, .30, 1.15, 'USB_D-_LINK'),
        smd_pad('6',  -0.25, -3.68, .30, 1.15, 'USB_D+_LINK'),
        smd_pad('7',   0.25, -3.68, .30, 1.15, 'USB_D-_LINK'),
        smd_pad('8',   0.75, -3.68, .30, 1.15, 'USB_D+_LINK'),
        smd_pad('9',   1.25, -3.68, .30, 1.15, None),
        smd_pad('10',  1.75, -3.68, .30, 1.15, 'USB_CC2'),
        smd_pad('11',  2.40, -3.68, .60, 1.15, None),
        smd_pad('12',  3.20, -3.68, .60, 1.15, 'GND'),
        '    (pad "" np_thru_hole circle (at -2.89 -2.605) (size 0.65 0.65) (drill 0.65) (layers "*.Cu" "*.Mask"))',
        '    (pad "" np_thru_hole circle (at 2.89 -2.605) (size 0.65 0.65) (drill 0.65) (layers "*.Cu" "*.Mask"))',
        '    (pad "SH" thru_hole oval (at -4.32 -3.105) (size 1 2.1) (drill oval 0.6 1.7) (layers "*.Cu" "*.Mask" "F.Paste") (net "GND"))',
        '    (pad "SH" thru_hole oval (at -4.32 1.075) (size 1 1.8) (drill oval 0.6 1.4) (layers "*.Cu" "*.Mask" "F.Paste") (net "GND"))',
        '    (pad "SH" thru_hole oval (at 4.32 -3.105) (size 1 2.1) (drill oval 0.6 1.7) (layers "*.Cu" "*.Mask" "F.Paste") (net "GND"))',
        '    (pad "SH" thru_hole oval (at 4.32 1.075) (size 1 1.8) (drill oval 0.6 1.4) (layers "*.Cu" "*.Mask" "F.Paste") (net "GND"))',
        '  )',
    ]
    return z


r = []

# --- R30 / LINK_BOOT above J3 ---
r += [
    seg('3V3_LINK', 'F.Cu', 93.50,22.80,93.00,21.30,.24),
    seg('LINK_BOOT','F.Cu',90.75,24.45,91.20,24.45),
    seg('LINK_BOOT','F.Cu',91.20,24.45,91.20,22.50),
    seg('LINK_BOOT','F.Cu',91.20,22.50,92.00,21.30),
    seg('LINK_BOOT','F.Cu',92.00,21.30,92.00,20.50),
    seg('LINK_BOOT','F.Cu',92.00,20.50,94.50,20.50),
]

# --- GNSS: preserve long In2 lanes, move only right escapes left of J3 ---
r += [
    # PPS: via at existing In2 endpoint, then short B/F escape to U2 pin16.
    via('GNSS_PPS',88.00,25.80),
    seg('GNSS_PPS','B.Cu',88.00,25.80,88.00,23.18,.20),
    via('GNSS_PPS',88.00,23.18),
    seg('GNSS_PPS','F.Cu',88.00,23.18,90.75,23.18,.20),

    # TX: extend In2 slightly, then private B.Cu vertical lane.
    seg('GNSS_TX','In2.Cu',88.00,27.80,89.40,27.80,.20),
    via('GNSS_TX',89.40,27.80),
    seg('GNSS_TX','B.Cu',89.40,27.80,89.40,11.75,.20),
    via('GNSS_TX',89.40,11.75),
    seg('GNSS_TX','F.Cu',89.40,11.75,90.75,11.75,.20),

    # RX: extend In2 to x=90.3, keeping its B.Cu lane separate from TX/PPS.
    seg('GNSS_RX','In2.Cu',87.00,28.40,90.30,28.40,.20),
    via('GNSS_RX',90.30,28.40),
    seg('GNSS_RX','B.Cu',90.30,28.40,90.30,13.02,.20),
    via('GNSS_RX',90.30,13.02),
    seg('GNSS_RX','F.Cu',90.30,13.02,90.75,13.02,.20),
]

# --- ACC_RAW: escape left from R10, then B.Cu under/below J3 to J4 ---
r += [
    seg('ACC_RAW','F.Cu',91.50,35.00,90.50,35.00,.28),
    via('ACC_RAW',90.50,35.00),
    seg('ACC_RAW','B.Cu',90.50,35.00,96.00,35.00,.28),
    seg('ACC_RAW','B.Cu',96.00,35.00,96.00,45.50,.28),
    via('ACC_RAW',96.00,45.50),
    seg('ACC_RAW','F.Cu',96.00,45.50,97.80,45.50,.28),
]

# --- USB D-/D+ from frozen R42/R43/test-point network to J3 ---
# D- leaves the existing R42 output on a short B.Cu detour so the two nets do
# not cross at their left-side launch, then both run as a separated F.Cu pair
# immediately below the Link module.
r += [
    seg('USB_D-_LINK','F.Cu',68.50,21.80,71.50,21.80,.22),
    via('USB_D-_LINK',71.50,21.80),
    seg('USB_D-_LINK','B.Cu',71.50,21.80,73.50,21.80,.22),
    seg('USB_D-_LINK','B.Cu',73.50,21.80,73.50,29.20,.22),
    via('USB_D-_LINK',73.50,29.20),
    seg('USB_D-_LINK','F.Cu',73.50,29.20,74.50,29.40,.22),
    seg('USB_D-_LINK','F.Cu',74.50,29.40,89.80,29.40,.22),
    seg('USB_D-_LINK','F.Cu',89.80,29.40,90.80,28.35,.22),
    seg('USB_D-_LINK','F.Cu',90.80,28.35,91.70,28.35,.22),

    # D- connects both reversible connector contacts on F.Cu.
    seg('USB_D-_LINK','F.Cu',91.70,28.35,92.645,28.35,.20),
    seg('USB_D-_LINK','F.Cu',91.70,28.35,91.70,29.35,.20),
    seg('USB_D-_LINK','F.Cu',91.70,29.35,92.645,29.35,.20),

    # D+ source launch and lower member of the long pair.
    seg('USB_D+_LINK','F.Cu',68.50,26.20,69.20,27.00,.22),
    seg('USB_D+_LINK','F.Cu',69.20,27.00,69.20,30.20,.22),
    seg('USB_D+_LINK','F.Cu',69.20,30.20,89.80,30.20,.22),
    seg('USB_D+_LINK','F.Cu',89.80,30.20,90.80,29.20,.22),
    via('USB_D+_LINK',90.80,29.20),

    # D+ reversible-contact merge on B.Cu, outside the pad row, then return at
    # x=90.8 so it never crosses the D- F.Cu merge.
    seg('USB_D+_LINK','B.Cu',90.80,29.20,93.60,28.85,.20),
    seg('USB_D+_LINK','F.Cu',92.645,28.85,93.60,28.85,.20),
    via('USB_D+_LINK',93.60,28.85),
    seg('USB_D+_LINK','B.Cu',93.60,28.85,93.60,27.85,.20),
    via('USB_D+_LINK',93.60,27.85),
    seg('USB_D+_LINK','F.Cu',92.645,27.85,93.60,27.85,.20),
]

r += j3_usb4105()

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied official-geometry USB-C service validation pass to {P}')
