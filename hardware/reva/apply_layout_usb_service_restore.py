from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Restore the Link USB-C service port for the production Rev.A.
# The original hand-built USB4105 footprint was removed during routing because
# its contact row sat too far outside the board. This pass uses the mechanical
# geometry of the GCT USB4105-xx-A family: PCB edge at local y=+3.675 mm,
# contact row at local y=-3.680 mm. At x=96.325, rot=270, the connector's
# nominal PCB-edge line lands exactly on the board edge x=100.000 mm.
#
# VBUS is intentionally NC: TruckBox is self-powered from the protected 24-V
# input, so USB must never back-power the board. CC1/CC2 each have 5.1-k Rd to
# GND. D+/D- retain the validated 22-R series resistors R42/R43 and add D6 ESD
# protection close to the connector.


def balanced_block(text, start):
    d = 0
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
            d += 1
        elif c == ')':
            d -= 1
            if d == 0:
                return j + 1
    raise RuntimeError('unterminated s-expression')


def remove_blocks_for_nets(text, tokens, nets):
    ranges = []
    for token in tokens:
        needle = '(' + token
        pos = 0
        while True:
            i = text.find(needle, pos)
            if i < 0:
                break
            j = balanced_block(text, i)
            b = text[i:j]
            if any(f'(net "{n}")' in b for n in nets):
                ranges.append((i, j))
            pos = j
    for i, j in sorted(ranges, reverse=True):
        text = text[:i] + text[j:]
    return text


def remove_ref(text, ref):
    marker = f'(property "Reference" "{ref}"'
    while marker in text:
        m = text.find(marker)
        i = text.rfind('(footprint ', 0, m)
        if i < 0:
            raise RuntimeError(f'footprint start not found for {ref}')
        j = balanced_block(text, i)
        text = text[:i] + text[j:]
    return text


def move_ref(text, ref, x, y, rot=0):
    marker = f'(property "Reference" "{ref}"'
    m = text.find(marker)
    if m < 0:
        raise RuntimeError(f'{ref} not found')
    i = text.rfind('(footprint ', 0, m)
    j = balanced_block(text, i)
    b = text[i:j]
    head = b.find(marker)
    pre = b[:head]
    repl = f'(at {x:g} {y:g}' + (f' {rot:g}' if rot else '') + ')'
    pre2, n = re.subn(r'\(at\s+[-+0-9.]+\s+[-+0-9.]+(?:\s+[-+0-9.]+)?\)', repl, pre, count=1)
    if n != 1:
        raise RuntimeError(f'placement not replaced for {ref}')
    return text[:i] + pre2 + b[head:] + text[j:]


def strip_old_r30_feed(text):
    pos = 0
    ranges = []
    while True:
        i = text.find('(segment', pos)
        if i < 0:
            break
        j = balanced_block(text, i)
        b = text[i:j]
        if '(net "3V3_LINK")' in b:
            pts = ('(start 93.5 24.5)' in b and '(end 93.5 22.8)' in b) or \
                  ('(end 93.5 24.5)' in b and '(start 93.5 22.8)' in b)
            if pts:
                ranges.append((i, j))
        pos = j
    for i, j in reversed(ranges):
        text = text[:i] + text[j:]
    return text


for ref in ('J3', 'D6', 'R1', 'R2'):
    s = remove_ref(s, ref)
s = remove_blocks_for_nets(s, ('segment', 'via'), {'GNSS_RX', 'GNSS_TX', 'GNSS_PPS', 'LINK_BOOT'})
s = strip_old_r30_feed(s)
s = move_ref(s, 'R30', 88.5, 33.0)


def seg(net, layer, x1, y1, x2, y2, w=.20):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "{net}"))')


def via(net, x, y, size=.55, drill=.30):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "{net}"))')


def fp_begin(name, ref, value, x, y, rot=0):
    r = f' {rot:g}' if rot else ''
    return [
        f'  (footprint "TruckBox:{name}" (layer "F.Cu")',
        f'    (at {x:g} {y:g}{r})',
        '    (attr smd)',
        f'    (fp_text reference "{ref}" (at 0 -4) (layer "F.SilkS") hide (effects (font (size 0.8 0.8) (thickness 0.12))))',
        f'    (fp_text value "{value}" (at 0 4) (layer "F.Fab") hide (effects (font (size 0.7 0.7) (thickness 0.1))))',
    ]


def smd_pad(num, x, y, sx, sy, net=None, shape='roundrect'):
    rr = ' (roundrect_rratio 0.2)' if shape == 'roundrect' else ''
    nn = f' (net "{net}")' if net else ''
    return (f'    (pad "{num}" smd {shape} (at {x:g} {y:g}) (size {sx:g} {sy:g}) '
            f'(layers "F.Cu" "F.Paste" "F.Mask"){rr}{nn})')


def fp_0603(ref, value, x, y, n1, n2):
    z = fp_begin('0603', ref, value, x, y)
    z += [
        '    (fp_rect (start -1.05 -0.65) (end 1.05 0.65) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))',
        smd_pad('1', -.5, 0, .65, .9, n1),
        smd_pad('2',  .5, 0, .65, .9, n2),
        '  )'
    ]
    return z


def fp_d6():
    z = fp_begin('SC70-3_DCK', 'D6', 'TPD2E2U06QDCKRQ1', 84.0, 31.0)
    z += [
        '    (fp_rect (start -1.25 -1.30) (end 1.25 1.30) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))',
        smd_pad('1', -1.0,  .65, .8, .6, 'USB_D+_LINK'),
        smd_pad('2', -1.0, -.65, .8, .6, 'USB_D-_LINK'),
        smd_pad('3',  1.0, 0.00, .8, .6, 'GND'),
        '  )'
    ]
    return z


def fp_usbc():
    z = fp_begin('USB_C_GCT_USB4105_16P', 'J3', 'USB4105-GF-A-060', 96.325, 28.8, 270)
    z += [
        '    (fp_rect (start -5.32 -4.76) (end 5.32 4.18) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))',
        '    (fp_rect (start -4.47 -3.675) (end 4.47 3.675) (stroke (width 0.10) (type solid)) (fill none) (layer "F.Fab"))',
        smd_pad('A1',  -3.20, -3.68, .60, 1.15, 'GND'),
        smd_pad('A4',  -2.40, -3.68, .60, 1.15, None),
        smd_pad('A5',  -1.25, -3.68, .30, 1.15, 'USB_CC1'),
        smd_pad('A6',  -0.25, -3.68, .30, 1.15, 'USB_D+_LINK'),
        smd_pad('A7',   0.25, -3.68, .30, 1.15, 'USB_D-_LINK'),
        smd_pad('A8',   1.25, -3.68, .30, 1.15, None),
        smd_pad('A9',   2.40, -3.68, .60, 1.15, None),
        smd_pad('A12',  3.20, -3.68, .60, 1.15, 'GND'),
        smd_pad('B1',   3.20, -3.68, .60, 1.15, 'GND'),
        smd_pad('B4',   2.40, -3.68, .60, 1.15, None),
        smd_pad('B5',   1.75, -3.68, .30, 1.15, 'USB_CC2'),
        smd_pad('B6',   0.75, -3.68, .30, 1.15, 'USB_D+_LINK'),
        smd_pad('B7',  -0.75, -3.68, .30, 1.15, 'USB_D-_LINK'),
        smd_pad('B8',  -1.75, -3.68, .30, 1.15, None),
        smd_pad('B9',  -2.40, -3.68, .60, 1.15, None),
        smd_pad('B12', -3.20, -3.68, .60, 1.15, 'GND'),
        '    (pad "" np_thru_hole circle (at -2.89 -2.605) (size 0.65 0.65) (drill 0.65) (layers "*.Cu" "*.Mask"))',
        '    (pad "" np_thru_hole circle (at 2.89 -2.605) (size 0.65 0.65) (drill 0.65) (layers "*.Cu" "*.Mask"))',
        '    (pad "SH" thru_hole oval (at -4.32 -3.105) (size 1.0 2.1) (drill oval 0.6 1.7) (layers "*.Cu" "*.Mask") (net "GND"))',
        '    (pad "SH" thru_hole oval (at -4.32 1.075) (size 1.0 1.8) (drill oval 0.6 1.4) (layers "*.Cu" "*.Mask") (net "GND"))',
        '    (pad "SH" thru_hole oval (at 4.32 -3.105) (size 1.0 2.1) (drill oval 0.6 1.7) (layers "*.Cu" "*.Mask") (net "GND"))',
        '    (pad "SH" thru_hole oval (at 4.32 1.075) (size 1.0 1.8) (drill oval 0.6 1.4) (layers "*.Cu" "*.Mask") (net "GND"))',
        '  )'
    ]
    return z


r = []
r += [
    seg('GNSS_RX', 'F.Cu', 54.85,20.30,56.10,20.30), via('GNSS_RX',56.10,20.30),
    seg('GNSS_RX','In2.Cu',56.10,20.30,60.20,20.30), seg('GNSS_RX','In2.Cu',60.20,20.30,60.20,28.40),
    seg('GNSS_RX','In2.Cu',60.20,28.40,87.00,28.40), via('GNSS_RX',87.00,28.40),
    seg('GNSS_RX','B.Cu',87.00,28.40,87.00,13.02), seg('GNSS_RX','B.Cu',87.00,13.02,90.00,13.02),
    via('GNSS_RX',90.00,13.02), seg('GNSS_RX','F.Cu',90.00,13.02,90.75,13.02),
    seg('GNSS_TX', 'F.Cu', 54.85,19.20,56.10,19.20), via('GNSS_TX',56.10,19.20),
    seg('GNSS_TX','In2.Cu',56.10,19.20,61.80,19.20), seg('GNSS_TX','In2.Cu',61.80,19.20,61.80,27.80),
    seg('GNSS_TX','In2.Cu',61.80,27.80,88.20,27.80), via('GNSS_TX',88.20,27.80),
    seg('GNSS_TX','B.Cu',88.20,27.80,88.20,11.75), seg('GNSS_TX','B.Cu',88.20,11.75,90.00,11.75),
    via('GNSS_TX',90.00,11.75), seg('GNSS_TX','F.Cu',90.00,11.75,90.75,11.75),
    seg('GNSS_PPS','F.Cu',54.85,18.10,56.10,18.10), via('GNSS_PPS',56.10,18.10),
    seg('GNSS_PPS','In2.Cu',56.10,18.10,63.00,18.10), seg('GNSS_PPS','In2.Cu',63.00,18.10,63.00,25.80),
    seg('GNSS_PPS','In2.Cu',63.00,25.80,89.20,25.80), via('GNSS_PPS',89.20,25.80),
    seg('GNSS_PPS','B.Cu',89.20,25.80,89.20,23.18), seg('GNSS_PPS','B.Cu',89.20,23.18,90.00,23.18),
    via('GNSS_PPS',90.00,23.18), seg('GNSS_PPS','F.Cu',90.00,23.18,90.75,23.18),
]

r += [
    seg('LINK_BOOT','F.Cu',94.50,20.50,91.835,20.50,.15),
    seg('LINK_BOOT','F.Cu',91.835,20.50,91.835,24.45,.15),
    seg('LINK_BOOT','F.Cu',91.835,24.45,90.75,24.45,.15),
    seg('LINK_BOOT','F.Cu',90.75,24.45,90.80,25.80,.18),
    via('LINK_BOOT',90.80,25.80),
    seg('LINK_BOOT','B.Cu',90.80,25.80,90.80,32.50,.18),
    via('LINK_BOOT',90.80,32.50),
    seg('LINK_BOOT','F.Cu',90.80,32.50,89.00,33.00,.18),
]

r += [
    seg('3V3_LINK','In2.Cu',93.50,22.80,95.30,22.80,.28),
    seg('3V3_LINK','In2.Cu',95.30,22.80,95.30,34.80,.28),
    seg('3V3_LINK','In2.Cu',95.30,34.80,88.00,34.80,.28),
    via('3V3_LINK',88.00,34.80,.70,.35),
    seg('3V3_LINK','F.Cu',88.00,34.80,88.00,33.00,.28),
]

r += [
    seg('USB_D-_LINK','F.Cu',92.645,28.55,94.00,28.55,.18), via('USB_D-_LINK',94.00,28.55),
    seg('USB_D-_LINK','F.Cu',92.645,29.55,94.00,29.55,.18), via('USB_D-_LINK',94.00,29.55),
    seg('USB_D-_LINK','B.Cu',94.00,28.55,94.00,29.55,.18),
    seg('USB_D-_LINK','B.Cu',94.00,29.55,91.80,29.70,.18), via('USB_D-_LINK',91.80,29.70),
    seg('USB_D-_LINK','F.Cu',91.80,29.70,87.00,29.70,.18),
    seg('USB_D-_LINK','F.Cu',87.00,29.70,83.00,30.35,.18),
    seg('USB_D-_LINK','F.Cu',83.00,30.35,72.50,29.70,.18),
    seg('USB_D-_LINK','F.Cu',72.50,29.70,69.50,23.00,.18),
    seg('USB_D-_LINK','F.Cu',69.50,23.00,68.50,23.00,.18),
    seg('USB_D+_LINK','F.Cu',92.645,28.05,91.40,28.05,.18),
    seg('USB_D+_LINK','F.Cu',91.40,28.05,90.00,29.20,.18),
    seg('USB_D+_LINK','F.Cu',90.00,29.20,90.00,30.50,.18),
    seg('USB_D+_LINK','F.Cu',92.645,29.05,91.40,29.05,.18),
    seg('USB_D+_LINK','F.Cu',91.40,29.05,90.00,30.50,.18),
    seg('USB_D+_LINK','F.Cu',90.00,30.50,86.50,32.20,.18),
    seg('USB_D+_LINK','F.Cu',86.50,32.20,83.00,31.65,.18),
    seg('USB_D+_LINK','F.Cu',83.00,31.65,72.50,30.50,.18),
    seg('USB_D+_LINK','F.Cu',72.50,30.50,69.50,25.00,.18),
    seg('USB_D+_LINK','F.Cu',69.50,25.00,68.50,25.00,.18),
    seg('GND','F.Cu',85.00,31.00,86.50,31.00,.30), via('GND',86.50,31.00,.70,.35),
    seg('USB_CC1','F.Cu',92.645,30.05,92.645,30.60,.18),
    seg('USB_CC1','F.Cu',92.645,30.60,95.00,30.60,.18),
    seg('USB_CC1','F.Cu',95.00,30.60,95.00,34.50,.18),
    seg('USB_CC1','F.Cu',95.00,34.50,94.00,36.00,.18),
    seg('USB_CC2','F.Cu',92.645,27.05,95.80,27.05,.18),
    seg('USB_CC2','F.Cu',95.80,27.05,95.80,34.50,.18),
    seg('USB_CC2','F.Cu',95.80,34.50,96.50,36.00,.18),
]

parts = []
parts += fp_usbc()
parts += fp_d6()
parts += fp_0603('R1','5.1k',94.5,36.0,'USB_CC1','GND')
parts += fp_0603('R2','5.1k',97.0,36.0,'USB_CC2','GND')

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(parts + r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Restored production USB-C service block in {P}')
