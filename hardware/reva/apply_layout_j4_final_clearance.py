from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Final local clearance pass for the 8-way right-angle Micro-Fit J4.
# Keep the Run-159 board frozen outside the J4 / ACC / CAN corner.
#
# Strategy:
# - keep J4 at the right board edge, centered at y=45.0 so its courtyard clears
#   the large D1 TVS below;
# - compact the small ACC front-end (Q3/R28/R10) above the J4 courtyard;
# - move D7 left, out of the J4 terminal field;
# - rebuild only the local ACC and J4-side CAN fanout;
# - explicitly delete stale vias left by the earlier vertical-J4 experiments.


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
required = (
    'GND', 'BATT24_FUSED',
    'ACC_RAW', 'ACC_MID1', 'ACC_BASE', 'ACC_N',
    'CAN1_H', 'CAN1_L', 'CAN2_H', 'CAN2_L',
)
for name in required:
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


def edge(a, b):
    return {tuple(round(v, 3) for v in a), tuple(round(v, 3) for v in b)}


# Mechanical placement.  J4 y=45.0 leaves ~0.6 mm courtyard gap to D1 while
# the three small ACC parts sit safely above the connector courtyard.
s = move_footprint(s, 'J4', 97.7, 45.0, 270)
s = move_footprint(s, 'Q3', 87.0, 34.5, 0)
s = move_footprint(s, 'R28', 89.9, 34.5, 0)
s = move_footprint(s, 'R10', 92.2, 34.5, 0)
s = move_footprint(s, 'D7', 77.5, 40.0, 0)

# Remove only the local routes superseded by this final placement.
remove_ranges = []
for a, b, block in iter_blocks(s, 'segment'):
    info = segment_info(block)
    if not info:
        continue
    name, p1, p2, layer = info
    e = edge(p1, p2)
    maxx = max(p1[0], p2[0])
    drop = False

    # Keep the validated transceiver-side trunks/stubs through x=83.2 and
    # rebuild everything from there to J4.
    if name in {'CAN1_H','CAN1_L','CAN2_H','CAN2_L'} and maxx > 83.21:
        drop = True

    # These three nets exist only inside the ACC front-end, so a complete local
    # rebuild is less error-prone than retaining fragments from earlier passes.
    if name in {'ACC_RAW','ACC_MID1','ACC_BASE'}:
        drop = True

    # Preserve the upstream ACC_N network through R12/C21; remove only the old
    # Q3 tail to the right of R12 pad 2 (x=84.5).
    if name == 'ACC_N' and maxx > 84.51:
        drop = True

    # Right-angle pass power/GND tails at the old J4 y=44 location.
    if name == 'BATT24_FUSED' and e in (
        {(92.23,48.5),(92.23,51.2)},
        {(92.23,51.2),(90.1,53.0)},
    ):
        drop = True
    if name == 'GND' and e == {(87.6,45.5),(86.7,45.5)}:
        drop = True

    if drop:
        remove_ranges.append((a, b))

for a, b in reversed(remove_ranges):
    s = s[:a] + s[b:]

# Remove stale/local vias.  Left-side CAN vias at x=82.2 remain as the handoff
# points to the already validated TVS/transceiver fanout.
via_remove = []
for a, b, block in iter_blocks(s, 'via'):
    ma = re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)\)', block)
    if not ma:
        continue
    xy = tuple(round(float(v), 3) for v in ma.groups())
    mn = re.search(r'\(net\s+"([^"]+)"\)', block)
    if mn:
        name = mn.group(1)
    else:
        mi = re.search(r'\(net\s+(\d+)\)', block)
        name = inv_net.get(int(mi.group(1))) if mi else None
    x = xy[0]
    drop = False

    if name in {'CAN1_H','CAN1_L','CAN2_H','CAN2_L'} and x > 83.21:
        drop = True
    if name == 'ACC_RAW':
        drop = True
    if name == 'GND' and xy in {(86.7,45.5),(96.0,49.5)}:
        drop = True
    if name == 'BATT24_FUSED' and xy == (91.29,49.25):
        drop = True

    if drop:
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


# J4 @ 97.7,45.0,270 global signal-pad centers:
#   p1 92.23,49.50  p2 92.23,46.50  p3 92.23,43.50  p4 92.23,40.50
#   p5 87.60,49.50  p6 87.60,46.50  p7 87.60,43.50  p8 87.60,40.50
new_items = [
    # CAN1_H / CAN1_L: separated B.Cu crossings back to the retained x=82.2
    # handoff vias.
    seg('CAN1_H',92.23,40.50,93.50,40.50,0.30,'F.Cu'),
    via('CAN1_H',93.50,40.50),
    seg('CAN1_H',93.50,40.50,82.20,41.85,0.30,'B.Cu'),

    seg('CAN1_L',92.23,43.50,93.50,43.50,0.30,'F.Cu'),
    via('CAN1_L',93.50,43.50),
    seg('CAN1_L',93.50,43.50,82.20,43.15,0.30,'B.Cu'),

    # CAN2_L stays on the back layer. CAN2_H uses a lower dogleg so the two
    # channels never cross and the large D2 input diode remains untouched.
    seg('CAN2_L',92.23,46.50,93.50,46.50,0.30,'F.Cu'),
    via('CAN2_L',93.50,46.50),
    seg('CAN2_L',93.50,46.50,82.20,49.15,0.30,'B.Cu'),

    seg('CAN2_H',87.60,49.50,88.50,50.50,0.30,'F.Cu'),
    via('CAN2_H',88.50,50.50),
    seg('CAN2_H',88.50,50.50,88.50,52.80,0.30,'B.Cu'),
    seg('CAN2_H',88.50,52.80,81.00,52.80,0.30,'B.Cu'),
    seg('CAN2_H',81.00,52.80,81.00,47.85,0.30,'B.Cu'),
    seg('CAN2_H',81.00,47.85,82.20,47.85,0.30,'B.Cu'),

    # +24 V: wide top trace into the existing D1/D2 protected-input backbone.
    seg('BATT24_FUSED',92.23,49.50,92.23,51.40,1.00,'F.Cu'),
    seg('BATT24_FUSED',92.23,51.40,90.10,53.00,1.00,'F.Cu'),

    # J4 GND gets a local plane stitch away from the retention holes.
    seg('GND',87.60,46.50,86.70,46.50,0.60,'F.Cu'),
    via('GND',86.70,46.50),

    # ACC_RAW: R10 drops to B.Cu at x=91.7. J4 escapes diagonally between its
    # own terminal rows, drops at x=90, then joins the same B.Cu spine at x=95.5.
    seg('ACC_RAW',91.70,34.50,91.70,35.50,0.24,'F.Cu'),
    via('ACC_RAW',91.70,35.50),
    seg('ACC_RAW',91.70,35.50,95.50,35.50,0.28,'B.Cu'),
    seg('ACC_RAW',87.60,43.50,88.60,44.20,0.24,'F.Cu'),
    seg('ACC_RAW',88.60,44.20,90.00,45.50,0.24,'F.Cu'),
    via('ACC_RAW',90.00,45.50),
    seg('ACC_RAW',90.00,45.50,95.50,45.50,0.28,'B.Cu'),
    seg('ACC_RAW',95.50,45.50,95.50,35.50,0.28,'B.Cu'),

    # ACC_MID1: both outer pads escape downward and meet at y=37, clear of the
    # USB-C service routing above.
    seg('ACC_MID1',89.40,34.50,89.40,37.00,0.22,'F.Cu'),
    seg('ACC_MID1',92.70,34.50,92.70,37.00,0.22,'F.Cu'),
    seg('ACC_MID1',89.40,37.00,92.70,37.00,0.22,'F.Cu'),

    # ACC_BASE: R28 and the relocated D7 meet on B.Cu at the central x=86.5
    # stitch. Top-layer branches then feed Q3 and R11 without passing through
    # R11's adjacent GND pad.
    seg('ACC_BASE',90.40,34.50,90.40,35.50,0.22,'F.Cu'),
    via('ACC_BASE',90.40,35.50),
    seg('ACC_BASE',90.40,35.50,90.40,38.00,0.24,'B.Cu'),
    seg('ACC_BASE',90.40,38.00,86.50,38.00,0.24,'B.Cu'),
    seg('ACC_BASE',78.55,40.00,79.30,40.00,0.22,'F.Cu'),
    via('ACC_BASE',79.30,40.00),
    seg('ACC_BASE',79.30,40.00,86.50,38.00,0.24,'B.Cu'),
    via('ACC_BASE',86.50,38.00),
    seg('ACC_BASE',86.50,38.00,85.95,35.45,0.22,'F.Cu'),
    seg('ACC_BASE',86.50,38.00,85.50,39.00,0.22,'F.Cu'),
    seg('ACC_BASE',85.50,39.00,83.50,39.00,0.22,'F.Cu'),
    seg('ACC_BASE',83.50,39.00,83.50,37.00,0.22,'F.Cu'),

    # ACC_N: preserve the upstream R12/C21 network; reconnect only Q3 pad 3
    # through a short B.Cu bridge below the USB_CC2 endpoint.
    seg('ACC_N',88.05,34.50,88.05,36.00,0.22,'F.Cu'),
    via('ACC_N',88.05,36.00),
    seg('ACC_N',88.05,36.00,84.30,35.80,0.22,'B.Cu'),
    via('ACC_N',84.30,35.80),
    seg('ACC_N',84.30,35.80,84.50,34.00,0.22,'F.Cu'),
]

close = s.rfind(')')
if close < 0:
    raise RuntimeError('board closing paren not found')
s = s[:close] + '\n' + '\n'.join(new_items) + '\n' + s[close:]

# Strict placement postconditions.
expected = {
    'J4': '(at 97.700 45.000 270)',
    'Q3': '(at 87.000 34.500 0)',
    'R28': '(at 89.900 34.500 0)',
    'R10': '(at 92.200 34.500 0)',
    'D7': '(at 77.500 40.000 0)',
}
for ref, at in expected.items():
    _, _, blk = find_footprint(s, ref)
    if at not in blk:
        raise RuntimeError(f'{ref} final placement failed')

P.write_text(s, encoding='utf-8')
print(f'Applied final right-angle J4 clearance pass to {P}')
