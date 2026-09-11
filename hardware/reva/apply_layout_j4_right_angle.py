from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Final J4 mechanical revision:
#   Molex Micro-Fit 3.0 430450806 / JLCPCB C587585
#   8-way, 2x4, 3.00 mm pitch, right-angle SMT.
#
# Pinout kept intentionally independent of the future CAN2 vehicle choice:
#   1 +24V       2 CAN2_L      3 CAN1_L      4 CAN1_H
#   5 CAN2_H     6 GND         7 ACC         8 RESERVE/NC
#
# Land/retention geometry follows Molex 43045-0806 family data:
#   signal lands 2.92 x 1.27 mm, 3.00 mm pitch;
#   local row Y = 5.47 / 10.10 mm from the retention-clip centerline;
#   retention holes X = +/-6.65 mm, drill = 2.41 mm.
#
# The connector is rotated 270 degrees so its mating body projects out of the
# right-hand PCB edge and its signal lands extend inward.  Only J4, D3/D4 and
# their immediate fanout are touched; the Run-159 validated board stays frozen
# everywhere else.


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
inv_net = {v: k for k, v in net_id.items()}


def net_expr(name, for_pad=False):
    if for_pad:
        return f'(net {net_id[name]} "{name}")'
    return f'(net {net_id[name]})'


def smd_pad(num, x, y, net=None):
    ne = (' ' + net_expr(net, True)) if net else ''
    # In this hand-generated board KiCad keeps the pad size axes absolute when
    # the footprint is transformed.  2.92 x 1.27 therefore leaves 1.73 mm
    # copper-to-copper pitch clearance along the 3.00 mm pin pitch after the
    # footprint rotation (the prior 1.27 x 2.92 declaration was incorrect).
    return (
        f'    (pad "{num}" smd roundrect (at {x:.3f} {y:.3f}) '
        f'(size 2.920 1.270) (layers "F.Cu" "F.Paste" "F.Mask") '
        f'(roundrect_rratio 0.15){ne})'
    )


def mp_pad(x):
    # Solderable retention clip / board-lock hole.  With J4 x=97.70 the outer
    # annular ring remains 0.65 mm inside the 100 mm board edge, satisfying the
    # project 0.50 mm edge-copper rule.
    return (
        f'    (pad "MP" thru_hole circle (at {x:.3f} 0.000) '
        f'(size 3.300 3.300) (drill 2.410) (layers "*.Cu" "*.Mask"))'
    )


j4_lines = [
    '  (footprint "TruckBox:Molex_Micro-Fit_3.0_43045-0806_2x04_P3.00mm_RightAngle" (layer "F.Cu")',
    '    (at 97.700 44.000 270)',
    '    (attr smd)',
    '    (fp_text reference "J4" (at 0 -6.2 270) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))',
    '    (fp_text value "Molex 430450806" (at 0 12.7 270) (layer "F.Fab") (effects (font (size 0.7 0.7) (thickness 0.1))))',
    # Housing/mating-face envelope from the Molex family drawing.  Negative
    # local Y is the mating side and therefore projects beyond x=100 after 270°.
    '    (fp_rect (start -7.825 -4.600) (end 7.825 3.710) (stroke (width 0.10) (type solid)) (fill none) (layer "F.Fab"))',
    '    (fp_line (start -7.825 -4.600) (end 7.825 -4.600) (stroke (width 0.20) (type solid)) (layer "F.SilkS"))',
    # Includes body, signal lands and both retention clips.  This deliberately
    # extends past the board edge on the mating side, which is normal for a
    # right-angle edge connector.
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

# Shift the two CAN TVS parts only far enough to open a real courtyard channel
# between them and the inward J4 land row.  D4 also moves 1 mm upward to clear
# D2's courtyard.
s = move_footprint(s, 'D3', 84.2, 42.5, 0)
s = move_footprint(s, 'D4', 84.2, 48.5, 0)


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


def edge(a, b):
    return {tuple(round(v, 3) for v in a), tuple(round(v, 3) for v in b)}


# Delete the complete former J4-side CAN fanout.  Rebuilding from the known
# U3/U4-side endpoints is safer than leaving fragments from the vertical-J4
# experiment.  Everything left of x=80 stays exactly as in Run 159.
remove = []
for a, b, block in iter_blocks(s, 'segment'):
    info = segment_info(block)
    if not info:
        continue
    name, p1, p2, layer = info
    pts = edge(p1, p2)
    maxx = max(p1[0], p2[0])
    drop = False

    if name in {'CAN1_H','CAN1_L','CAN2_H','CAN2_L'} and maxx >= 80.0:
        drop = True

    # Old vertical-J4 power/ACC/GND tails.  Preserve the established backbone
    # nodes at BATT 90.1,53 and ACC B.Cu 90.5,35 -> 96,35.
    if name == 'BATT24_FUSED' and pts == {(88.8,52.5),(90.1,53.0)}:
        drop = True
    if name == 'GND' and pts == {(98.2,49.5),(96.0,49.5)}:
        drop = True
    if name == 'ACC_RAW' and layer == 'F.Cu' and maxx >= 96.0:
        drop = True

    if drop:
        remove.append((a, b))

for a, b in reversed(remove):
    s = s[:a] + s[b:]

# Remove only CAN vias belonging to the superseded J4 fanout/cleanup.  Other
# global stitching vias are untouched.
via_remove = []
for a, b, block in iter_blocks(s, 'via'):
    ma = re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)\)', block)
    if not ma:
        continue
    mn = re.search(r'\(net\s+"([^"]+)"\)', block)
    if mn:
        name = mn.group(1)
    else:
        mi = re.search(r'\(net\s+(\d+)\)', block)
        name = inv_net.get(int(mi.group(1))) if mi else None
    x = float(ma.group(1))
    if name in {'CAN1_H','CAN1_L','CAN2_H','CAN2_L'} and x >= 80.0:
        via_remove.append((a, b))
for a, b in reversed(via_remove):
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


# Expected global signal-pad centers after the 270° J4 rotation:
#   p1 92.23,48.50   p2 92.23,45.50   p3 92.23,42.50   p4 92.23,39.50
#   p5 87.60,48.50   p6 87.60,45.50   p7 87.60,42.50   p8 87.60,39.50
#
# D3 @84.2,42.5: H 83.2,41.85 / L 83.2,43.15 / GND 85.2,42.5
# D4 @84.2,48.5: H 83.2,47.85 / L 83.2,49.15 / GND 85.2,48.5
new_items = [
    # Restore the four validated transceiver-side CAN trunks to the relocated
    # TVS lands.  These short F.Cu runs contain no layer transitions.
    seg('CAN1_H',74.70,41.865,83.20,41.850,0.30,'F.Cu'),
    seg('CAN1_L',74.70,43.135,83.20,43.150,0.30,'F.Cu'),
    seg('CAN2_H',74.70,47.865,83.20,47.850,0.30,'F.Cu'),
    seg('CAN2_L',74.70,49.135,83.20,49.150,0.30,'F.Cu'),

    # CAN1-H: J4 escape -> back-layer crossing -> D3 pad.
    seg('CAN1_H',92.23,39.50,93.50,39.50,0.30,'F.Cu'),
    via('CAN1_H',93.50,39.50),
    seg('CAN1_H',93.50,39.50,82.20,41.85,0.30,'B.Cu'),
    via('CAN1_H',82.20,41.85),
    seg('CAN1_H',82.20,41.85,83.20,41.85,0.30,'F.Cu'),

    # CAN1-L: parallel, vertically separated back-layer route.
    seg('CAN1_L',92.23,42.50,93.50,42.50,0.30,'F.Cu'),
    via('CAN1_L',93.50,42.50),
    seg('CAN1_L',93.50,42.50,82.20,43.15,0.30,'B.Cu'),
    via('CAN1_L',82.20,43.15),
    seg('CAN1_L',82.20,43.15,83.20,43.15,0.30,'F.Cu'),

    # CAN2-L uses B.Cu for the long run.
    seg('CAN2_L',92.23,45.50,93.50,45.50,0.30,'F.Cu'),
    via('CAN2_L',93.50,45.50),
    seg('CAN2_L',93.50,45.50,82.20,49.15,0.30,'B.Cu'),
    via('CAN2_L',82.20,49.15),
    seg('CAN2_L',82.20,49.15,83.20,49.15,0.30,'F.Cu'),

    # CAN2-H starts on the inward J4 row; dogleg away from D4's GND pad before
    # dropping to B.Cu.
    seg('CAN2_H',87.60,48.50,86.70,47.20,0.30,'F.Cu'),
    via('CAN2_H',86.70,47.20),
    seg('CAN2_H',86.70,47.20,82.20,47.85,0.30,'B.Cu'),
    via('CAN2_H',82.20,47.85),
    seg('CAN2_H',82.20,47.85,83.20,47.85,0.30,'F.Cu'),

    # +24 V: short, wide top-layer route into the existing protected-input
    # backbone node.  No power routing elsewhere is changed.
    seg('BATT24_FUSED',92.23,48.50,92.23,51.20,1.00,'F.Cu'),
    seg('BATT24_FUSED',92.23,51.20,90.10,53.00,1.00,'F.Cu'),

    # GND: direct stitch into the continuous inner ground plane.
    seg('GND',87.60,45.50,86.70,45.50,0.60,'F.Cu'),
    via('GND',86.70,45.50),

    # ACC: escape toward the right on F.Cu, then run on B.Cu outside the CAN
    # corridor to the existing ACC_RAW backbone endpoint at 96,35.
    seg('ACC_RAW',87.60,42.50,89.50,42.50,0.28,'F.Cu'),
    seg('ACC_RAW',89.50,42.50,89.50,44.20,0.28,'F.Cu'),
    via('ACC_RAW',89.50,44.20),
    seg('ACC_RAW',89.50,44.20,96.00,44.20,0.28,'B.Cu'),
    seg('ACC_RAW',96.00,44.20,96.00,35.00,0.28,'B.Cu'),
]

close = s.rfind(')')
if close < 0:
    raise RuntimeError('board closing paren not found')
s = s[:close] + '\n' + '\n'.join(new_items) + '\n' + s[close:]

# Strict postconditions catch stale/partial CI generations before KiCad sees
# them.
_, _, chk = find_footprint(s, 'J4')
if '430450806' not in chk or '(at 97.700 44.000 270)' not in chk:
    raise RuntimeError('right-angle J4 replacement failed')
for ref, expected in [('D3','(at 84.200 42.500 0)'),('D4','(at 84.200 48.500 0)')]:
    _, _, blk = find_footprint(s, ref)
    if expected not in blk:
        raise RuntimeError(f'{ref} final placement failed')

P.write_text(s, encoding='utf-8')
print(f'Applied corrected right-angle Micro-Fit J4 revision to {P}')
