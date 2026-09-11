from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Final automotive-input protection pass.
#
# D1 is the unidirectional SM8S33A load-dump TVS.  The previous prototype
# footprint incorrectly used two equal pads.  DO-218AB uses a large metal
# heatsink/anode land and a small lead/cathode land.  D1 is also moved to the
# fused raw input, ahead of D2, so load-dump current does not have to pass
# through the 2 A series reverse-polarity Schottky.
#
# Project diode numbering convention in this board remains:
#   pad 1 = anode / metal heatsink = GND
#   pad 2 = cathode              = BATT24_FUSED
# External B+ fuse is mandatory; reverse battery forward-biases D1 and the
# fuse is the intended fault-current interrupter.


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
    for marker in (f'(property "Reference" "{ref}"',
                   f'(fp_text reference "{ref}"'):
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


def block_net(block):
    m = re.search(r'\(net(?:\s+\d+)?\s+"([^"]+)"\)', block)
    return m.group(1) if m else None


def block_coords(block):
    return [tuple(map(float, m)) for m in re.findall(
        r'\((?:start|end|at)\s+(-?[\d.]+)\s+(-?[\d.]+)', block)]


def remove_blocks(text, token, pred):
    ranges = []
    pos = 0
    needle = '(' + token
    while True:
        i = text.find(needle, pos)
        if i < 0:
            break
        j = balanced_block(text, i)
        block = text[i:j]
        if pred(block):
            ranges.append((i, j))
        pos = j
    for i, j in reversed(ranges):
        text = text[:i] + text[j:]
    return text


def seg(net, layer, x1, y1, x2, y2, width):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {width:.3f}) '
            f'(layer "{layer}") (net "{net}"))')


def via(net, x, y, size=1.10, drill=.55):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") '
            f'(net "{net}"))')


# Remove old D1 and only the obsolete VIN_PROT spur that used to feed it.
s = remove_ref(s, 'D1')
obsolete_vin = {
    ((79.65, 53.0), (81.0, 56.0)),
    ((81.0, 56.0), (80.0, 59.0)),
    ((80.0, 59.0), (77.5, 60.0)),
}


def close(p, q):
    return abs(p[0] - q[0]) < .002 and abs(p[1] - q[1]) < .002


def obsolete_vin_segment(block):
    if block_net(block) != 'VIN_PROT':
        return False
    pts = block_coords(block)
    if len(pts) < 2:
        return False
    a, b = pts[0], pts[1]
    return any((close(a, x) and close(b, y)) or
               (close(a, y) and close(b, x))
               for x, y in obsolete_vin)


s = remove_blocks(s, 'segment', obsolete_vin_segment)

r = []

# Yangzhou Yangjie DO-218AB suggested solder-pad geometry, using nominal
# dimensions from the current manufacturer drawing:
#   large heatsink/anode land = 9.3 x 10.0 mm
#   inter-land gap            = 3.1 mm
#   small lead/cathode land   = 2.6 x 2.7 mm
# Overall copper-land span is 15.0 mm.
r += [
    '  (footprint "TruckBox:DO218AB_Yangjie_SM8S" (layer "F.Cu")',
    '    (at 86.000 59.500)',
    '    (attr smd)',
    '    (fp_text reference "D1" (at 0 -6.2) (layer "F.Fab") hide (effects (font (size 0.8 0.8) (thickness 0.12))))',
    '    (fp_text value "SM8S33A" (at 0 6.2) (layer "F.Fab") hide (effects (font (size 0.7 0.7) (thickness 0.1))))',
    '    (fp_rect (start -6.85 -5.25) (end 6.85 5.25) (stroke (width 0.10) (type solid)) (fill none) (layer "F.Fab"))',
    '    (fp_rect (start -7.75 -5.50) (end 7.75 5.50) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))',
    '    (pad "1" smd roundrect (at -2.850 0) (size 9.300 10.000) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.04) (net "GND"))',
    '    (pad "2" smd roundrect (at 6.200 0) (size 2.600 2.700) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.08) (net "BATT24_FUSED"))',
    '  )',

    # Cathode branches directly from fused raw B+ before D2.
    seg('BATT24_FUSED', 'F.Cu', 92.200, 53.000, 92.200, 59.500, 1.20),

    # Robust low-inductance connection from the large anode land to the solid
    # L2 GND plane.  Vias are beside the pad, not in it, to avoid solder wicking.
    seg('GND', 'F.Cu', 87.800, 55.500, 88.600, 55.500, .80),
    via('GND', 88.600, 55.500),
    seg('GND', 'F.Cu', 87.800, 57.300, 88.600, 57.300, .80),
    via('GND', 88.600, 57.300),
    seg('GND', 'F.Cu', 87.800, 61.700, 88.600, 61.700, .80),
    via('GND', 88.600, 61.700),
    seg('GND', 'F.Cu', 87.800, 63.500, 88.600, 63.500, .80),
    via('GND', 88.600, 63.500),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied final automotive input TVS protection to {P}')
