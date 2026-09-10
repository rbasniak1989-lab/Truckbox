from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# USB-C production rebuild v3.
# KiCad board-file pad angles are absolute. J3 is rotated 90 deg, therefore
# every non-circular J3 pad/slot also carries angle 90.
net_id = {name: int(i) for i, name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', s)}
id_net = {i: name for name, i in net_id.items()}


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
    hits = []
    for marker in (f'(property "Reference" "{ref}"', f'(fp_text reference "{ref}"'):
        p = text.find(marker)
        if p >= 0:
            hits.append(p)
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
    rel = m - i
    pre, post = b[:rel], b[rel:]
    repl = f'(at {x:g} {y:g}' + (f' {rot:g}' if rot else '') + ')'
    pre, n = re.subn(r'\(at\s+[-+0-9.]+\s+[-+0-9.]+(?:\s+[-+0-9.]+)?\)', repl, pre, count=1)
    if n != 1:
        raise RuntimeError(f'placement not replaced for {ref}')
    return text[:i] + pre + post + text[j:]


def block_net(b):
    m = re.search(r'\(net(?:\s+\d+)?\s+"([^"]+)"\)', b)
    if m:
        return m.group(1)
    m = re.search(r'\(net\s+(\d+)\)', b)
    return id_net.get(int(m.group(1))) if m else None


def block_layer(b):
    m = re.search(r'\(layer\s+"([^"]+)"\)', b)
    return m.group(1) if m else None


def block_points(b):
    return [(float(x), float(y)) for x, y in re.findall(
        r'\((?:start|end|at)\s+([-+0-9.]+)\s+([-+0-9.]+)', b)]


def has_point(b, x, y, tol=.012):
    return any(abs(px-x) <= tol and abs(py-y) <= tol for px, py in block_points(b))


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


# Fine-pitch USB-C solder-mask bridges inside this footprint are intentional.
if '(allow_soldermask_bridges_in_footprints' in s:
    s = re.sub(r'\(allow_soldermask_bridges_in_footprints\s+(?:yes|no)\)',
               '(allow_soldermask_bridges_in_footprints yes)', s)
else:
    s = s.replace('(setup (pad_to_mask_clearance 0))',
                  '(setup (pad_to_mask_clearance 0) (allow_soldermask_bridges_in_footprints yes))')

# Rebuild the local production block deterministically, removing both numeric
# pre-KiCad routes and named routes left by the previous validation pass.
for ref in ('J3', 'D6', 'R1', 'R2'):
    s = remove_ref(s, ref)
s = move_ref(s, 'R30', 94.0, 21.3, 0)

for net in ('USB_D-_LINK', 'USB_D+_LINK', 'LINK_BOOT', 'ACC_RAW'):
    s = remove_blocks(s, 'segment', lambda b, n=net: block_net(b) == n)
    s = remove_blocks(s, 'via', lambda b, n=net: block_net(b) == n)

# Remove all right-side GNSS escapes and their terminal In2 segments. Source
# fanout and the long lanes up to x~60 stay frozen.
gnss = {'GNSS_PPS', 'GNSS_TX', 'GNSS_RX'}
s = remove_blocks(
    s, 'segment',
    lambda b: block_net(b) in gnss and block_points(b) and (
        (block_layer(b) in ('F.Cu', 'B.Cu') and max(x for x, _ in block_points(b)) >= 85.0) or
        (block_layer(b) == 'In2.Cu' and max(x for x, _ in block_points(b)) >= 85.0)
    )
)
s = remove_blocks(
    s, 'via',
    lambda b: block_net(b) in gnss and block_points(b) and block_points(b)[0][0] >= 85.0
)

# Remove only the old R30 3V3_LINK tail; preserve its B.Cu trunk/via.
s = remove_blocks(
    s, 'segment',
    lambda b: block_net(b) == '3V3_LINK' and block_layer(b) == 'F.Cu'
    and any(x >= 93.0 and y >= 20.0 for x, y in block_points(b))
)

# Old GND stitch occupied the new USB breakout corridor.
s = remove_blocks(s, 'via',
                  lambda b: block_net(b) == 'GND' and has_point(b, 90.0, 30.0))

# CORE_TO_LINK's original y=31 In2 leg crosses J3's lower guide hole.
s = remove_blocks(
    s, 'segment',
    lambda b: block_net(b) == 'CORE_TO_LINK' and block_layer(b) == 'In2.Cu'
    and has_point(b, 40.5, 31.0) and has_point(b, 94.5, 31.0)
)


def seg(net, layer, x1, y1, x2, y2, w=.20):
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
        f'    (fp_text reference "{ref}" (at 0 -5.5 {rot:g}) (layer "F.SilkS") hide (effects (font (size 0.8 0.8) (thickness 0.12))))',
        f'    (fp_text value "{value}" (at 0 5 {rot:g}) (layer "F.Fab") hide (effects (font (size 0.7 0.7) (thickness 0.1))))',
    ]


def smd_pad(num, x, y, sx, sy, net=None, zone_solid=False):
    nn = f' (net "{net}")' if net else ''
    zc = ' (zone_connect 2)' if zone_solid else ''
    return (f'    (pad "{num}" smd roundrect (at {x:g} {y:g} 90) (size {sx:g} {sy:g}) '
            f'(layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25){zc}{nn})')


def j3_usb4105():
    z = fp_begin('USB_C_Receptacle_GCT_USB4105_16P', 'J3',
                 'USB4105-GF-A-060', 96.325, 28.600, 90)
    z += [
        '    (fp_line (start 5 3.675) (end -5 3.675) (stroke (width 0.1) (type solid)) (layer "Dwgs.User"))',
        smd_pad('A1',  -3.20, -3.68, .60, 1.15, 'GND', True),
        smd_pad('B12', -3.20, -3.68, .60, 1.15, 'GND', True),
        smd_pad('A4',  -2.40, -3.68, .60, 1.15, None),
        smd_pad('B9',  -2.40, -3.68, .60, 1.15, None),
        smd_pad('B8',  -1.75, -3.68, .30, 1.15, None),
        smd_pad('A5',  -1.25, -3.68, .30, 1.15, 'USB_CC1'),
        smd_pad('B7',  -0.75, -3.68, .30, 1.15, 'USB_D-_LINK'),
        smd_pad('A6',  -0.25, -3.68, .30, 1.15, 'USB_D+_LINK'),
        smd_pad('A7',   0.25, -3.68, .30, 1.15, 'USB_D-_LINK'),
        smd_pad('B6',   0.75, -3.68, .30, 1.15, 'USB_D+_LINK'),
        smd_pad('A8',   1.25, -3.68, .30, 1.15, None),
        smd_pad('B5',   1.75, -3.68, .30, 1.15, 'USB_CC2'),
        smd_pad('A9',   2.40, -3.68, .60, 1.15, None),
        smd_pad('B4',   2.40, -3.68, .60, 1.15, None),
        smd_pad('A12',  3.20, -3.68, .60, 1.15, 'GND', True),
        smd_pad('B1',   3.20, -3.68, .60, 1.15, 'GND', True),
        '    (pad "" np_thru_hole circle (at -2.89 -2.605) (size 0.65 0.65) (drill 0.65) (layers "*.Cu" "*.Mask"))',
        '    (pad "" np_thru_hole circle (at 2.89 -2.605) (size 0.65 0.65) (drill 0.65) (layers "*.Cu" "*.Mask"))',
        '    (pad "SH" thru_hole oval (at -4.32 -3.105 90) (size 1 2.1) (drill oval 0.6 1.7) (layers "*.Cu" "*.Mask" "F.Paste") (zone_connect 2) (net "GND"))',
        '    (pad "SH" thru_hole oval (at -4.32 1.075 90) (size 1 1.8) (drill oval 0.6 1.4) (layers "*.Cu" "*.Mask" "F.Paste") (zone_connect 2) (net "GND"))',
        '    (pad "SH" thru_hole oval (at 4.32 -3.105 90) (size 1 2.1) (drill oval 0.6 1.7) (layers "*.Cu" "*.Mask" "F.Paste") (zone_connect 2) (net "GND"))',
        '    (pad "SH" thru_hole oval (at 4.32 1.075 90) (size 1 1.8) (drill oval 0.6 1.4) (layers "*.Cu" "*.Mask" "F.Paste") (zone_connect 2) (net "GND"))',
        '  )',
    ]
    return z


r = [
    # R30 / LINK_BOOT.
    seg('3V3_LINK', 'F.Cu', 93.50,22.80,93.50,21.30,.22),
    seg('LINK_BOOT', 'F.Cu', 90.75,24.45,89.00,24.45,.20),
    via('LINK_BOOT', 89.00,24.45),
    seg('LINK_BOOT', 'B.Cu', 89.00,24.45,89.00,35.00,.20),
    seg('LINK_BOOT', 'B.Cu', 89.00,35.00,95.20,35.00,.20),
    seg('LINK_BOOT', 'B.Cu', 95.20,35.00,95.20,21.30,.20),
    via('LINK_BOOT', 95.20,21.30),
    seg('LINK_BOOT', 'F.Cu', 95.20,21.30,94.50,21.30,.20),

    # GNSS shifted left.
    seg('GNSS_PPS','In2.Cu',63.00,25.80,87.50,25.80,.20),
    via('GNSS_PPS',87.50,25.80),
    seg('GNSS_PPS','B.Cu',87.50,25.80,87.50,23.18,.20),
    via('GNSS_PPS',87.50,23.18),
    seg('GNSS_PPS','F.Cu',87.50,23.18,90.75,23.18,.20),
    seg('GNSS_TX','In2.Cu',61.80,27.80,85.50,27.80,.20),
    via('GNSS_TX',85.50,27.80),
    seg('GNSS_TX','B.Cu',85.50,27.80,85.50,11.75,.20),
    via('GNSS_TX',85.50,11.75),
    seg('GNSS_TX','F.Cu',85.50,11.75,90.75,11.75,.20),
    seg('GNSS_RX','In2.Cu',60.20,28.40,86.50,28.40,.20),
    via('GNSS_RX',86.50,28.40),
    seg('GNSS_RX','B.Cu',86.50,28.40,86.50,13.02,.20),
    via('GNSS_RX',86.50,13.02),
    seg('GNSS_RX','F.Cu',86.50,13.02,90.75,13.02,.20),

    # CORE_TO_LINK around the lower guide hole.
    seg('CORE_TO_LINK','In2.Cu',40.50,31.00,80.00,31.00,.20),
    via('CORE_TO_LINK',80.00,31.00),
    seg('CORE_TO_LINK','B.Cu',80.00,31.00,80.00,34.00,.20),
    seg('CORE_TO_LINK','B.Cu',80.00,34.00,94.50,34.00,.20),
    seg('CORE_TO_LINK','B.Cu',94.50,34.00,94.50,30.50,.20),
    via('CORE_TO_LINK',94.50,30.50),

    # ACC_RAW below connector.
    seg('ACC_RAW','F.Cu',91.50,35.00,91.50,36.00,.28),
    seg('ACC_RAW','F.Cu',91.50,36.00,96.00,36.00,.28),
    seg('ACC_RAW','F.Cu',96.00,36.00,96.00,45.50,.28),
    seg('ACC_RAW','F.Cu',96.00,45.50,97.80,45.50,.28),

    # Main USB pair from R42/R43.
    seg('USB_D-_LINK','F.Cu',68.50,23.00,70.00,23.00,.20),
    seg('USB_D-_LINK','F.Cu',70.00,23.00,70.00,29.80,.20),
    seg('USB_D-_LINK','F.Cu',70.00,29.80,89.50,29.80,.20),
    seg('USB_D+_LINK','F.Cu',68.50,25.00,69.00,25.00,.20),
    seg('USB_D+_LINK','F.Cu',69.00,25.00,69.00,30.60,.20),
    seg('USB_D+_LINK','F.Cu',69.00,30.60,90.80,30.60,.20),

    # Fine-pitch USB fanout: four individual escapes, then same-net B.Cu merge.
    seg('USB_D+_LINK','F.Cu',92.645,27.850,91.850,27.850,.14),
    seg('USB_D+_LINK','F.Cu',91.850,27.850,91.400,27.000,.14),
    via('USB_D+_LINK',91.400,27.000,.45,.20),
    seg('USB_D-_LINK','F.Cu',92.645,28.350,91.850,28.350,.14),
    seg('USB_D-_LINK','F.Cu',91.850,28.350,90.400,27.600,.14),
    via('USB_D-_LINK',90.400,27.600,.45,.20),
    seg('USB_D+_LINK','F.Cu',92.645,28.850,91.850,28.850,.14),
    seg('USB_D+_LINK','F.Cu',91.850,28.850,91.400,30.000,.14),
    via('USB_D+_LINK',91.400,30.000,.45,.20),
    seg('USB_D-_LINK','F.Cu',92.645,29.350,91.850,29.350,.14),
    seg('USB_D-_LINK','F.Cu',91.850,29.350,90.400,30.800,.14),
    via('USB_D-_LINK',90.400,30.800,.45,.20),
    seg('USB_D+_LINK','B.Cu',91.400,27.000,91.400,30.000,.16),
    seg('USB_D-_LINK','B.Cu',90.400,27.600,90.400,30.800,.16),
    seg('USB_D-_LINK','F.Cu',90.400,27.600,89.500,27.600,.16),
    seg('USB_D-_LINK','F.Cu',89.500,27.600,89.500,29.800,.16),
    seg('USB_D+_LINK','F.Cu',91.400,30.000,90.800,30.600,.16),
]

r += j3_usb4105()
pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied USB-C production rebuild v3 to {P}')
