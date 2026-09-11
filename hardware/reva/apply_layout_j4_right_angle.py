from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Final J4 mechanical revision:
# Molex Micro-Fit 3.0 430450806 / JLCPCB C587585, 8-way right-angle SMT.
# The right-angle body exits the board edge, avoiding the D1/J3 collision that
# the larger vertical 430450818 courtyard created.  Pin assignment is unchanged:
#   1 +24V, 2 CAN2_L, 3 CAN1_L, 4 CAN1_H,
#   5 CAN2_H, 6 GND, 7 ACC, 8 NC/RESERVE.
# Recommended land geometry follows Molex SD-43045-003 / 43045-0806:
#   signal lands 1.27 x 2.92 mm, 3.00 mm pitch;
#   row Y = 5.47 / 10.10 mm from clip-hole centerline;
#   solderable retention clips at X = +/-6.65 mm, drill 2.41 mm.


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


def iter_blocks(text, token):
    i = 0
    needle = '(' + token
    while True:
        i = text.find(needle, i)
        if i < 0:
            return
        j = balanced_block(text, i)
        yield i, j, text[i:j]
        i = j


def ref_in_footprint(block, ref):
    return (
        re.search(r'\(property\s+"Reference"\s+"' + re.escape(ref) + r'"', block) is not None
        or re.search(r'\(fp_text\s+reference\s+"?' + re.escape(ref) + r'"?(?:\s|\))', block) is not None
    )


def find_footprint(text, ref):
    for i, j, block in iter_blocks(text, 'footprint'):
        if ref_in_footprint(block, ref):
            return i, j, block
    raise RuntimeError(f'footprint {ref} not found')


def move_footprint(text, ref, x, y, rot=0):
    i, j, block = find_footprint(text, ref)
    nb, n = re.subn(
        r'\(at\s+[-+0-9.]+\s+[-+0-9.]+(?:\s+[-+0-9.]+)?\)',
        f'(at {x:.3f} {y:.3f} {rot})',
        block,
        count=1,
    )
    if n != 1:
        raise RuntimeError(f'could not move footprint {ref}')
    return text[:i] + nb + text[j:]


net_id = {name: int(idx) for idx, name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', s)}
for required in ('GND','BATT24_FUSED','ACC_RAW','CAN1_H','CAN1_L','CAN2_H','CAN2_L'):
    if required not in net_id:
        raise RuntimeError(f'net {required} not found')
inv_net = {v:k for k,v in net_id.items()}


def net_expr(name, for_pad=False):
    if for_pad:
        return f'(net {net_id[name]} "{name}")'
    return f'(net {net_id[name]})'


def smd_pad(num, x, y, net=None):
    ne = (' ' + net_expr(net, True)) if net else ''
    # Molex land is 1.27 x 2.92 in the local coordinate system.
    return (
        f'    (pad "{num}" smd roundrect (at {x:.3f} {y:.3f}) '
        f'(size 1.270 2.920) (layers "F.Cu" "F.Paste" "F.Mask") '
        f'(roundrect_rratio 0.15){ne})'
    )


def mp_pad(x):
    # Solderable retention clip.  2.41 mm drill per Molex drawing; annular ring
    # retained so JLC can solder the clip for vibration resistance.
    return (
        f'    (pad "MP" thru_hole circle (at {x:.3f} 0.000) '
        f'(size 3.300 3.300) (drill 2.410) (layers "*.Cu" "*.Mask"))'
    )


# Origin is the centerline between the two retention clips.  Rotated 90 degrees
# at x=98.70 so both 2.41-mm clip holes remain fully inside the 100-mm board,
# while the mating body projects beyond the right board edge.  y=44.00 centers
# the 16-mm body between J3 (above) and D1 (below).
j4_lines = [
    '  (footprint "TruckBox:Molex_Micro-Fit_3.0_43045-0806_2x04_P3.00mm_RightAngle" (layer "F.Cu")',
    '    (at 98.700 44.000 90)',
    '    (attr smd)',
    '    (fp_text reference "J4" (at 0 -6.2 90) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))',
    '    (fp_text value "Molex 430450806" (at 0 12.7 90) (layer "F.Fab") (effects (font (size 0.7 0.7) (thickness 0.1))))',
    # Approximate housing outline from SD-43045-003; the mating face is on -Y.
    '    (fp_rect (start -7.825 -4.600) (end 7.825 3.710) (stroke (width 0.10) (type solid)) (fill none) (layer "F.Fab"))',
    '    (fp_line (start -7.825 -4.600) (end 7.825 -4.600) (stroke (width 0.20) (type solid)) (layer "F.SilkS"))',
    # Realistic assembly courtyard includes housing, lands and retention clips.
    '    (fp_rect (start -8.400 -4.850) (end 8.400 11.810) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))',
    smd_pad(1, 4.5, 5.47, 'BATT24_FUSED'),
    smd_pad(2, 1.5, 5.47, 'CAN2_L'),
    smd_pad(3,-1.5, 5.47, 'CAN1_L'),
    smd_pad(4,-4.5, 5.47, 'CAN1_H'),
    smd_pad(5, 4.5,10.10, 'CAN2_H'),
    smd_pad(6, 1.5,10.10, 'GND'),
    smd_pad(7,-1.5,10.10, 'ACC_RAW'),
    smd_pad(8,-4.5,10.10, None),
    mp_pad(-6.65),
    mp_pad( 6.65),
    '  )',
]
j4 = '\n'.join(j4_lines)
i, j, _old = find_footprint(s, 'J4')
s = s[:i] + j4 + s[j:]

# One extra millimeter of leftward clearance gives the connector courtyard more
# than 1 mm separation from D3/D4 while preserving the validated CAN fanout.
s = move_footprint(s, 'D3', 84.5, 42.5, 0)
s = move_footprint(s, 'D4', 84.5, 49.5, 0)


def segment_info(block):
    ms = re.search(r'\(start\s+([-+0-9.]+)\s+([-+0-9.]+)\)', block)
    me = re.search(r'\(end\s+([-+0-9.]+)\s+([-+0-9.]+)\)', block)
    ml = re.search(r'\(layer\s+"([^"]+)"\)', block)
    if not (ms and me and ml):
        return None
    mn = re.search(r'\(net\s+"([^"]+)"\)', block)
    if mn:
        name = mn.group(1)
    else:
        mi = re.search(r'\(net\s+(\d+)\)', block)
        if not mi:
            return None
        name = inv_net.get(int(mi.group(1)))
    return name, tuple(map(float, ms.groups())), tuple(map(float, me.groups())), ml.group(1)


# Remove all previous vertical-J4 CAN fanout while retaining the frozen
# transceiver-side trunks (their opposite endpoint is x<=80/74.7).  Also remove
# the old J4 BATT/ACC/GND stubs.
remove = []
for a, b, block in iter_blocks(s, 'segment'):
    info = segment_info(block)
    if not info:
        continue
    name, p1, p2, layer = info
    minx = min(p1[0], p2[0])
    pts = {tuple(round(v,3) for v in p1), tuple(round(v,3) for v in p2)}
    drop = False
    if name in {'CAN1_H','CAN1_L','CAN2_H','CAN2_L'} and minx >= 84.4:
        drop = True
    if name == 'BATT24_FUSED' and pts == {(88.8,52.5),(90.1,53.0)}:
        drop = True
    if name == 'ACC_RAW' and pts == {(98.2,46.5),(97.8,45.5)}:
        drop = True
    if name == 'GND' and pts == {(98.2,49.5),(96.0,49.5)}:
        drop = True
    if drop:
        remove.append((a,b))
for a,b in reversed(remove):
    s = s[:a] + s[b:]

# Remove vias created only for the previous vertical-J4 fanout.
via_remove = []
for a, b, block in iter_blocks(s, 'via'):
    ma = re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)\)', block)
    if not ma:
        continue
    xy = tuple(round(float(v),3) for v in ma.groups())
    if xy in {(96.0,49.5),(96.0,52.5),(86.8,48.0)}:
        via_remove.append((a,b))
for a,b in reversed(via_remove):
    s = s[:a] + s[b:]


def seg(name, x1, y1, x2, y2, width=0.30, layer='F.Cu'):
    return (
        f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
        f'(width {width:.3f}) (layer "{layer}") {net_expr(name)})'
    )


def via(name, x, y):
    return (
        f'  (via (at {x:.3f} {y:.3f}) (size 0.700) (drill 0.350) '
        f'(layers "F.Cu" "B.Cu") {net_expr(name)})'
    )


# Global J4 signal-pad centers after 90-degree rotation:
#   p1 93.23,48.50  p2 93.23,45.50  p3 93.23,42.50  p4 93.23,39.50
#   p5 88.60,48.50  p6 88.60,45.50  p7 88.60,42.50  p8 88.60,39.50
# D3/D4 CAN pad centers after the 1-mm left shift:
#   CAN1_H 83.50,41.85; CAN1_L 83.50,43.15
#   CAN2_H 83.50,48.85; CAN2_L 83.50,50.15
new_items = [
    # CAN1 outer-row pads escape right, cross on B.Cu, and rejoin the existing
    # validated F.Cu trunks through vias just left of the TVS pads.
    seg('CAN1_H',93.23,39.50,95.00,39.50,0.30,'F.Cu'),
    via('CAN1_H',95.00,39.50),
    seg('CAN1_H',95.00,39.50,82.50,41.85,0.30,'B.Cu'),
    via('CAN1_H',82.50,41.85),
    seg('CAN1_H',82.50,41.85,83.50,41.85,0.30,'F.Cu'),

    seg('CAN1_L',93.23,42.50,95.00,42.50,0.30,'F.Cu'),
    via('CAN1_L',95.00,42.50),
    seg('CAN1_L',95.00,42.50,82.50,43.15,0.30,'B.Cu'),
    via('CAN1_L',82.50,43.15),
    seg('CAN1_L',82.50,43.15,83.50,43.15,0.30,'F.Cu'),

    # CAN2_L needs the crossover; CAN2_H can remain a short direct top trace.
    seg('CAN2_L',93.23,45.50,95.00,45.50,0.30,'F.Cu'),
    via('CAN2_L',95.00,45.50),
    seg('CAN2_L',95.00,45.50,82.50,50.15,0.30,'B.Cu'),
    via('CAN2_L',82.50,50.15),
    seg('CAN2_L',82.50,50.15,83.50,50.15,0.30,'F.Cu'),
    seg('CAN2_H',88.60,48.50,83.50,48.85,0.30,'F.Cu'),

    # +24 V follows the lower edge of the connector and joins the established
    # protected-input node at 90.1,53.0.
    seg('BATT24_FUSED',93.23,48.50,93.23,51.50,1.00,'F.Cu'),
    seg('BATT24_FUSED',93.23,51.50,90.10,53.00,1.00,'F.Cu'),

    # GND gets a dedicated stitch straight into the continuous inner plane.
    seg('GND',88.60,45.50,87.20,45.50,0.60,'F.Cu'),
    via('GND',87.20,45.50),

    # ACC crosses on the back layer to the existing ACC_RAW backbone at 90.5,35.
    seg('ACC_RAW',88.60,42.50,88.60,41.00,0.28,'F.Cu'),
    via('ACC_RAW',88.60,41.00),
    seg('ACC_RAW',88.60,41.00,90.50,35.00,0.28,'B.Cu'),
]

close = s.rfind(')')
if close < 0:
    raise RuntimeError('board closing paren not found')
s = s[:close] + '\n' + '\n'.join(new_items) + '\n' + s[close:]

# Postconditions: exact final connector and local placements.
_, _, chk = find_footprint(s, 'J4')
if '430450806' not in chk or '(at 98.700 44.000 90)' not in chk:
    raise RuntimeError('right-angle J4 replacement failed')
for ref, expected in [('D3','(at 84.500 42.500 0)'),('D4','(at 84.500 49.500 0)')]:
    _, _, blk = find_footprint(s, ref)
    if expected not in blk:
        raise RuntimeError(f'{ref} final placement failed')

P.write_text(s, encoding='utf-8')
print(f'Applied right-angle Micro-Fit J4 final revision to {P}')
