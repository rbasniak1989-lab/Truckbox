from pathlib import Path
import re
import runpy

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Final local cleanup for the assembled Micro-Fit J4 revision.
# Keep the Run-159 board frozen everywhere except the minimum J4-adjacent
# copper/placement changes required for manufacturing DRC.


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
for required in ('BATT24_FUSED', 'ACC_MID1', 'ACC_BASE', 'CAN2_H'):
    if required not in net_id:
        raise RuntimeError(f'net {required} not found')
inv_net = {v: k for k, v in net_id.items()}


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


# Restore D1 to its Run-159 validated location. The temporary 3 mm left move
# caused a C3/D1 courtyard collision; J4 does not require that D1 move.
s = move_footprint(s, 'D1', 83.9, 59.5, 0)

remove_ranges = []
for a, b, block in iter_blocks(s, 'segment'):
    info = segment_info(block)
    if not info:
        continue
    name, p1, p2, layer = info
    e = edge(p1, p2)
    drop = False

    # Temporary D1 relocation jumper.
    if name == 'BATT24_FUSED' and e == {(90.1, 59.5), (87.1, 59.5)}:
        drop = True

    # R28 reroute from the previous pass plus the old ACC_BASE tail.
    if name == 'ACC_MID1' and e in (
        {(92.1, 36.0), (91.4, 36.0)},
        {(91.4, 36.0), (90.4, 37.0)},
    ):
        drop = True
    if name == 'ACC_BASE' and e in (
        {(91.4, 37.0), (91.4, 38.0)},
        {(89.05, 38.0), (92.5, 38.0)},
    ):
        drop = True

    # CAN2_H must cross under the J4 pad field on B.Cu. Remove the temporary
    # top-layer path that squeezed between the 3 mm-pitch signal pads.
    if name == 'CAN2_H' and layer == 'F.Cu' and e in (
        {(98.2, 52.5), (92.5, 52.5)},
        {(92.5, 52.5), (92.5, 48.0)},
        {(92.5, 48.0), (86.8, 48.0)},
    ):
        drop = True

    if drop:
        remove_ranges.append((a, b))

for a, b in reversed(remove_ranges):
    s = s[:a] + s[b:]


def seg(name, x1, y1, x2, y2, width=0.30, layer='F.Cu'):
    return (
        f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
        f'(width {width:.3f}) (layer "{layer}") (net {net_id[name]}))'
    )


def via(name, x, y):
    return (
        f'  (via (at {x:.3f} {y:.3f}) (size 0.700) (drill 0.350) '
        f'(layers "F.Cu" "B.Cu") (net {net_id[name]}))'
    )


new_items = [
    # R28: approach each pad orthogonally with generous inter-net clearance.
    seg('ACC_MID1', 92.1, 36.0, 90.4, 36.0, 0.25),
    seg('ACC_MID1', 90.4, 36.0, 90.4, 37.0, 0.25),
    seg('ACC_BASE', 89.05, 38.0, 91.4, 38.0, 0.22),
    seg('ACC_BASE', 91.4, 38.0, 91.4, 37.0, 0.22),

    # CAN2_H: short F.Cu escape from J4, B.Cu crossing under the connector,
    # then return to the already validated D4-side F.Cu fanout.
    seg('CAN2_H', 98.2, 52.5, 96.0, 52.5, 0.30, 'F.Cu'),
    via('CAN2_H', 96.0, 52.5),
    seg('CAN2_H', 96.0, 52.5, 86.8, 48.0, 0.30, 'B.Cu'),
    via('CAN2_H', 86.8, 48.0),
]

close = s.rfind(')')
if close < 0:
    raise RuntimeError('board closing paren not found')
s = s[:close] + '\n' + '\n'.join(new_items) + '\n' + s[close:]

# Strict postconditions for the local revision.
_, _, d1 = find_footprint(s, 'D1')
if '(at 83.900 59.500 0)' not in d1:
    raise RuntimeError('D1 restore failed')
_, _, r28 = find_footprint(s, 'R28')
if '(at 90.900 37.000 0)' not in r28:
    raise RuntimeError('R28 location unexpectedly changed')
_, _, j4 = find_footprint(s, 'J4')
if '430450818' not in j4:
    raise RuntimeError('J4 Micro-Fit footprint missing')

P.write_text(s, encoding='utf-8')
print(f'Applied final J4 DRC cleanup to {P}')

# The right-angle variant is the actual manufacturing choice. Keeping it as a
# separate final pass makes the Run-159-derived cleanup auditable and lets CI
# validate the final 430450806 geometry with no hidden manual edits.
runpy.run_path(str(Path(__file__).with_name('apply_layout_j4_right_angle.py')), run_name='__main__')
