from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Final automotive-input protection pass.
#
# D1 is the unidirectional SM8S33A load-dump TVS. DO-218AB uses a large
# metal heatsink/anode land and a small lead/cathode land. D1 is connected
# to the fused raw input, ahead of D2, so load-dump current does not pass
# through the 2 A series reverse-polarity Schottky.
#
# Project diode numbering convention in this board:
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


def move_ref(text, ref, x, y):
    m = ref_marker_pos(text, ref)
    if m < 0:
        raise RuntimeError(f'{ref} not found')
    i = text.rfind('(footprint ', 0, m)
    if i < 0:
        raise RuntimeError(f'footprint start not found for {ref}')
    j = balanced_block(text, i)
    b = text[i:j]
    head = min([p for p in (
        b.find(f'(property "Reference" "{ref}"'),
        b.find(f'(fp_text reference "{ref}"')) if p >= 0])
    pre = b[:head]
    mat = list(re.finditer(r'\(at\s+(-?[\d.]+)\s+(-?[\d.]+)(?:\s+(-?[\d.]+))?\)', pre))
    if not mat:
        raise RuntimeError(f'placement not found for {ref}')
    a = mat[0]
    rot = a.group(3)
    repl = f'(at {x:.3f} {y:.3f}' + (f' {rot}' if rot is not None else '') + ')'
    b = b[:a.start()] + repl + b[a.end():]
    return text[:i] + b + text[j:]


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


def close(p, q):
    return abs(p[0] - q[0]) < .003 and abs(p[1] - q[1]) < .003


def matches_edge(block, p1, p2):
    pts = block_coords(block)
    if len(pts) < 2:
        return False
    a, b = pts[0], pts[1]
    return ((close(a, p1) and close(b, p2)) or
            (close(a, p2) and close(b, p1)))


def seg(net, layer, x1, y1, x2, y2, width):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {width:.3f}) '
            f'(layer "{layer}") (net "{net}"))')


def via(net, x, y, size=1.10, drill=.55):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") '
            f'(net "{net}"))')


# Deterministic rebuild of only the lower-right input-protection block.
s = remove_ref(s, 'D1')
s = move_ref(s, 'D2', 82.0, 52.0)

# Remove the previous D2 input route and every old VIN_PROT branch leaving
# D2. Match by geometry instead of net text because early generator passes
# use numeric net IDs before KiCad normalizes the board.
old_edges = [
    ((97.8, 53.0), (84.35, 53.0)),        # old BATT -> D2
    ((79.65, 53.0), (78.0, 55.0)),        # old VIN main route
    ((78.0, 55.0), (71.0, 55.0)),
    ((79.65, 53.0), (81.0, 56.0)),        # old TVS VIN spur
    ((81.0, 56.0), (80.0, 59.0)),
    ((80.0, 59.0), (77.5, 60.0)),
]

s = remove_blocks(
    s, 'segment',
    lambda b: any(matches_edge(b, a, z) for a, z in old_edges)
)

r = []

# Yangzhou Yangjie DO-218AB nominal suggested-land geometry:
#   large heatsink/anode land = 9.3 x 10.0 mm
#   inter-land gap            = 3.1 mm
#   small lead/cathode land   = 2.6 x 2.7 mm
# D1 shifts left to clear J4 mechanically. D2 shifts 1 mm upward to give the
# full DO-218AB body/courtyard a clean mechanical gap.
r += [
    '  (footprint "TruckBox:DO218AB_Yangjie_SM8S" (layer "F.Cu")',
    '    (at 83.900 59.500)',
    '    (attr smd)',
    '    (fp_text reference "D1" (at 0 -6.2) (layer "F.Fab") hide (effects (font (size 0.8 0.8) (thickness 0.12))))',
    '    (fp_text value "SM8S33A" (at 0 6.2) (layer "F.Fab") hide (effects (font (size 0.7 0.7) (thickness 0.1))))',
    '    (fp_rect (start -6.85 -5.25) (end 6.85 5.25) (stroke (width 0.10) (type solid)) (fill none) (layer "F.Fab"))',
    '    (fp_rect (start -7.75 -5.50) (end 7.75 5.50) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))',
    '    (pad "1" smd roundrect (at -2.850 0) (size 9.300 10.000) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.04) (net "GND"))',
    '    (pad "2" smd roundrect (at 6.200 0) (size 2.600 2.700) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.08) (net "BATT24_FUSED"))',
    '  )',

    # Raw B+ from J4 first reaches the TVS branch with a wide surge path.
    seg('BATT24_FUSED', 'F.Cu', 97.800, 53.000, 90.100, 53.000, 1.20),
    seg('BATT24_FUSED', 'F.Cu', 90.100, 53.000, 90.100, 59.500, 1.20),

    # Normal operating-current branch from the raw-B+ node to D2 anode.
    seg('BATT24_FUSED', 'F.Cu', 90.100, 53.000, 86.500, 52.000, .80),
    seg('BATT24_FUSED', 'F.Cu', 86.500, 52.000, 84.350, 52.000, .80),

    # D2 cathode -> VIN_PROT. Route left above D1, then rejoin the frozen VIN
    # network at (71,55), clear of C3 and the TVS anode land.
    seg('VIN_PROT', 'F.Cu', 79.650, 52.000, 76.400, 52.000, .60),
    seg('VIN_PROT', 'F.Cu', 76.400, 52.000, 75.200, 53.400, .55),
    seg('VIN_PROT', 'F.Cu', 75.200, 53.400, 71.000, 53.400, .55),
    seg('VIN_PROT', 'F.Cu', 71.000, 53.400, 71.000, 55.000, .55),

    # Low-inductance anode return into the solid L2 ground plane. Vias remain
    # outside the solder land to avoid solder wicking during assembly.
    seg('GND', 'F.Cu', 85.700, 55.500, 86.500, 55.500, .80),
    via('GND', 86.500, 55.500),
    seg('GND', 'F.Cu', 85.700, 57.300, 86.500, 57.300, .80),
    via('GND', 86.500, 57.300),
    seg('GND', 'F.Cu', 85.700, 61.700, 86.500, 61.700, .80),
    via('GND', 86.500, 61.700),
    seg('GND', 'F.Cu', 85.700, 63.500, 86.500, 63.500, .80),
    via('GND', 86.500, 63.500),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied final automotive input TVS protection to {P}')
