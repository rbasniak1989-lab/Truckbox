from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Restore the production USB protection / Type-C sink block after the local
# J3 rebuild.  Run 144 is the frozen 0-real-DRC / 0-open baseline.
# - D6 is a shunt ESD device: its I/O pads land directly on the already-routed
#   D-/D+ pair, so the validated pair geometry is not broken.
# - R1/R2 are the required 5.1k Rd pull-downs for CC1/CC2.
# - All three parts remain on F.Cu for single-sided assembly.


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


def block_net(b):
    m = re.search(r'\(net(?:\s+\d+)?\s+"([^"]+)"\)', b)
    if m:
        return m.group(1)
    return None


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


def seg(net, layer, x1, y1, x2, y2, w=.18):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "{net}"))')


def via(net, x, y, size=.55, drill=.30):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "{net}"))')


def fp_0603(ref, value, x, y, rot, net1, net2):
    # KiCad board-file pad angles are absolute.  Coordinates are footprint-local.
    return [
        f'  (footprint "TruckBox:0603" (layer "F.Cu")',
        f'    (at {x:.3f} {y:.3f} {rot:g})',
        '    (attr smd)',
        f'    (fp_text reference "{ref}" (at 0 -1.4 {rot:g}) (layer "F.SilkS") (effects (font (size 0.7 0.7) (thickness 0.12))))',
        f'    (fp_text value "{value}" (at 0 1.4 {rot:g}) (layer "F.Fab") hide (effects (font (size 0.6 0.6) (thickness 0.1))))',
        '    (fp_rect (start -0.800 -0.400) (end 0.800 0.400) (stroke (width 0.12) (type solid)) (fill none) (layer "F.SilkS"))',
        '    (fp_rect (start -1.050 -0.650) (end 1.050 0.650) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))',
        f'    (pad "1" smd roundrect (at -0.500 0 {rot:g}) (size 0.650 0.900) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) (net "{net1}"))',
        f'    (pad "2" smd roundrect (at 0.500 0 {rot:g}) (size 0.650 0.900) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) (net "{net2}"))',
        '  )',
    ]


def fp_d6():
    # 45 deg clockwise in KiCad. Global pad centers after transform:
    # P1 D- ~= (87.253,32.320), P2 D+ ~= (86.333,31.400),
    # P3 GND ~= (88.207,30.446).
    return [
        '  (footprint "TruckBox:SC70-3_DCK" (layer "F.Cu")',
        '    (at 87.500 31.153 45)',
        '    (attr smd)',
        '    (fp_text reference "D6" (at 0 -2.0 45) (layer "F.SilkS") (effects (font (size 0.7 0.7) (thickness 0.12))))',
        '    (fp_text value "TPD2E2U06Q" (at 0 2.0 45) (layer "F.Fab") hide (effects (font (size 0.6 0.6) (thickness 0.1))))',
        '    (fp_rect (start -1.000 -1.050) (end 1.000 1.050) (stroke (width 0.12) (type solid)) (fill none) (layer "F.SilkS"))',
        '    (fp_rect (start -1.250 -1.300) (end 1.250 1.300) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))',
        '    (pad "1" smd roundrect (at -1.000 0.650 45) (size 0.800 0.600) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) (net "USB_D-_LINK"))',
        '    (pad "2" smd roundrect (at -1.000 -0.650 45) (size 0.800 0.600) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) (net "USB_D+_LINK"))',
        '    (pad "3" smd roundrect (at 1.000 0 45) (size 0.800 0.600) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) (net "GND"))',
        '  )',
    ]


# Deterministic rebuild of only the missing USB support block.
for ref in ('D6', 'R1', 'R2'):
    s = remove_ref(s, ref)
for net in ('USB_CC1', 'USB_CC2'):
    s = remove_blocks(s, 'segment', lambda b, n=net: block_net(b) == n)
    s = remove_blocks(s, 'via', lambda b, n=net: block_net(b) == n)

r = []
r += fp_d6()
r += fp_0603('R2', '5.1k', 80.500, 34.900, 180, 'USB_CC2', 'GND')
r += fp_0603('R1', '5.1k', 80.500, 37.000, 180, 'USB_CC1', 'GND')

# D6: short low-inductance ground return. I/O pads directly overlap the
# already-frozen D-/D+ tracks; no USB pair segment is removed or lengthened.
r += [
    seg('GND', 'F.Cu', 88.207, 30.446, 87.500, 29.750, .20),
    via('GND', 87.500, 29.750),

    # CC1: escape below the connector, then L3 through the clear corridor.
    seg('USB_CC1', 'F.Cu', 92.645, 29.850, 91.700, 30.700, .15),
    seg('USB_CC1', 'F.Cu', 91.700, 30.700, 91.700, 33.500, .15),
    seg('USB_CC1', 'F.Cu', 91.700, 33.500, 90.500, 33.500, .15),
    via('USB_CC1', 90.500, 33.500),
    seg('USB_CC1', 'In2.Cu', 90.500, 33.500, 89.900, 33.500, .15),
    seg('USB_CC1', 'In2.Cu', 89.900, 33.500, 89.900, 37.000, .15),
    seg('USB_CC1', 'In2.Cu', 89.900, 37.000, 81.500, 37.000, .18),
    via('USB_CC1', 81.500, 37.000),
    seg('USB_CC1', 'F.Cu', 81.500, 37.000, 81.000, 37.000, .18),

    # CC2: the narrow source escape clears LINK_BOOT and USB D+; B.Cu then
    # doglegs around the ACC_RAW via and the frozen Core/Link UART via.
    seg('USB_CC2', 'F.Cu', 92.645, 26.850, 91.900, 27.220, .15),
    via('USB_CC2', 91.900, 27.220),
    seg('USB_CC2', 'B.Cu', 91.900, 27.220, 91.900, 34.400, .15),
    seg('USB_CC2', 'B.Cu', 91.900, 34.400, 90.000, 34.400, .15),
    seg('USB_CC2', 'B.Cu', 90.000, 34.400, 90.000, 34.650, .15),
    seg('USB_CC2', 'B.Cu', 90.000, 34.650, 88.800, 34.650, .15),
    seg('USB_CC2', 'B.Cu', 88.800, 34.650, 88.800, 34.900, .15),
    seg('USB_CC2', 'B.Cu', 88.800, 34.900, 81.500, 34.900, .18),
    via('USB_CC2', 81.500, 34.900),
    seg('USB_CC2', 'F.Cu', 81.500, 34.900, 81.000, 34.900, .18),

    # Local ground returns for Rd resistors.
    seg('GND', 'F.Cu', 80.000, 37.000, 79.200, 37.000, .20),
    via('GND', 79.200, 37.000),
    seg('GND', 'F.Cu', 80.000, 34.900, 79.200, 34.900, .20),
    via('GND', 79.200, 34.900),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Restored USB D6 ESD + R1/R2 CC pull-down block in {P}')
