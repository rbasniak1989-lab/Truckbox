from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Run-168 follow-up: keep the accepted J4/CAN geometry and resolve the remaining
# ACC-corner collisions only.  No changes are made outside the local right-side
# service/ACC region.


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
for name in ('ACC_RAW','ACC_MID1','ACC_BASE','ACC_N'):
    if name not in net_id:
        raise RuntimeError(f'net {name} not found')
inv_net = {v: k for k, v in net_id.items()}


def net_expr(name):
    return f'(net {net_id[name]})'


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


# Placement changes are deliberately small. Q3 moves upward away from the old
# GND stitch; R28/R10 move downward to clear LINK/USB/J3; D7 moves right of the
# CAN_MODE via. All remain above/left of J4's conservative courtyard.
s = move_footprint(s, 'Q3', 87.0, 33.5, 0)
s = move_footprint(s, 'R28', 89.9, 35.5, 0)
s = move_footprint(s, 'R10', 92.2, 35.5, 0)
s = move_footprint(s, 'D7', 78.5, 40.0, 0)

# Rebuild the four ACC local nets from scratch while preserving the long ACC_N
# trunk and its R12 connection (everything up to x=84.5).
remove_ranges = []
for a, b, block in iter_blocks(s, 'segment'):
    info = segment_info(block)
    if not info:
        continue
    name, p1, p2, layer = info
    maxx = max(p1[0], p2[0])
    if name in {'ACC_RAW','ACC_MID1','ACC_BASE'}:
        remove_ranges.append((a,b))
    elif name == 'ACC_N' and maxx > 84.51:
        remove_ranges.append((a,b))
for a,b in reversed(remove_ranges):
    s = s[:a] + s[b:]

# Remove vias belonging to the superseded local ACC fanout, plus four known
# stale stitching/fanout vias reported by the Run-168 DRC. Coordinate matching
# is intentionally tolerant because older scripts used non-rounded positions.
stale_xy = [
    (86.00,36.00),   # old GND stitch beside Q3
    (94.00,36.00),   # stale ACC_RAW via
    (91.29,49.25),   # stale BATT24_FUSED via
    (91.68,44.25),   # stale CAN1_L via
]

def near(xy, target, tol=.12):
    return abs(xy[0]-target[0]) <= tol and abs(xy[1]-target[1]) <= tol

via_remove = []
for a,b,block in iter_blocks(s,'via'):
    ma = re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)\)', block)
    if not ma:
        continue
    xy = tuple(map(float, ma.groups()))
    mn = re.search(r'\(net\s+"([^"]+)"\)', block)
    if mn:
        name = mn.group(1)
    else:
        mi = re.search(r'\(net\s+(\d+)\)', block)
        name = inv_net.get(int(mi.group(1))) if mi else None
    drop = False
    if name in {'ACC_RAW','ACC_MID1','ACC_BASE'}:
        drop = True
    if name == 'ACC_N' and xy[0] > 80.0:
        drop = True
    if any(near(xy,t) for t in stale_xy):
        drop = True
    if drop:
        via_remove.append((a,b))
for a,b in reversed(via_remove):
    s = s[:a] + s[b:]


def seg(name,x1,y1,x2,y2,width=.22,layer='F.Cu'):
    return (
        f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
        f'(width {width:.3f}) (layer "{layer}") {net_expr(name)})'
    )


def via(name,x,y,size=.70,drill=.35):
    return (
        f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
        f'(layers "F.Cu" "B.Cu") {net_expr(name)})'
    )

# New pad centers after this pass:
# Q3 p1=85.95,33.45  p3=88.05,33.50
# R28 p1=89.40,35.50 p2=90.40,35.50
# R10 p1=91.70,35.50 p2=92.70,35.50
# D7 p2=79.55,40.00
items = [
    # ACC_MID1 is now a direct short top-layer link; no vias required.
    seg('ACC_MID1',89.40,35.50,92.70,35.50,.22,'F.Cu'),

    # ACC_RAW: move the upper transition rightward away from USB_CC1, then use
    # the already-clear x=95.5 B.Cu spine to J4 p7.
    seg('ACC_RAW',91.70,35.50,92.50,36.20,.24,'F.Cu'),
    via('ACC_RAW',92.50,36.20),
    seg('ACC_RAW',92.50,36.20,95.50,36.20,.28,'B.Cu'),
    seg('ACC_RAW',87.60,43.50,88.60,44.20,.24,'F.Cu'),
    seg('ACC_RAW',88.60,44.20,90.00,45.50,.24,'F.Cu'),
    via('ACC_RAW',90.00,45.50),
    seg('ACC_RAW',90.00,45.50,95.50,45.50,.28,'B.Cu'),
    seg('ACC_RAW',95.50,45.50,95.50,36.20,.28,'B.Cu'),

    # ACC_BASE: four short top escapes feed a B.Cu star. This keeps copper away
    # from the retained ACC_N trunk at 83.5,40 and removes the old GND-via clash.
    seg('ACC_BASE',90.40,35.50,90.40,36.50,.22,'F.Cu'),
    via('ACC_BASE',90.40,36.50),
    seg('ACC_BASE',90.40,36.50,86.50,38.00,.24,'B.Cu'),

    seg('ACC_BASE',85.95,33.45,84.50,35.50,.22,'F.Cu'),
    via('ACC_BASE',84.50,35.50),
    seg('ACC_BASE',84.50,35.50,86.50,38.00,.24,'B.Cu'),

    seg('ACC_BASE',83.50,37.00,84.80,38.20,.22,'F.Cu'),
    via('ACC_BASE',84.80,38.20),
    seg('ACC_BASE',84.80,38.20,86.50,38.00,.24,'B.Cu'),

    seg('ACC_BASE',79.55,40.00,80.30,40.00,.22,'F.Cu'),
    via('ACC_BASE',80.30,40.00),
    seg('ACC_BASE',80.30,40.00,86.50,38.00,.24,'B.Cu'),

    # ACC_N: direct F.Cu connection from Q3 p3 to the retained R12 pad.  This
    # avoids both the removed GND stitch and the USB_CC2 B.Cu corridor.
    seg('ACC_N',88.05,33.50,86.50,33.50,.22,'F.Cu'),
    seg('ACC_N',86.50,33.50,84.50,34.00,.22,'F.Cu'),
]

close=s.rfind(')')
if close < 0:
    raise RuntimeError('board closing paren not found')
s=s[:close]+'\n'+'\n'.join(items)+'\n'+s[close:]

expected={
    'Q3':'(at 87.000 33.500 0)',
    'R28':'(at 89.900 35.500 0)',
    'R10':'(at 92.200 35.500 0)',
    'D7':'(at 78.500 40.000 0)',
    'J4':'(at 97.700 45.000 270)',
}
for ref,at in expected.items():
    _,_,blk=find_footprint(s,ref)
    if at not in blk:
        raise RuntimeError(f'{ref} placement postcondition failed')

P.write_text(s,encoding='utf-8')
print(f'Applied Run-168 final ACC/J4 DRC cleanup to {P}')
