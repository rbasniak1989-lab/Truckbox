from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Conservative final USB refinement.
#
# Keep the entire already-proven connector-side USB routing (J3 <-> D6 <->
# R42/R43 output nets) exactly as produced by the previous passes.  Only move
# and rotate the two 22R series resistors so their MCU-side pads face U2, then
# replace the MCU-side copper with short F.Cu routes.  This removes four USB
# data vias while preserving the run151 connector fanout that already passed
# real KiCad DRC at 0 geometry/electrical and 0 unconnected.
#
# R42 = D- : pad1 USB_D-_LINK_MCU, pad2 USB_D-_LINK
# R43 = D+ : pad1 USB_D+_LINK_MCU, pad2 USB_D+_LINK
# New placement at x=70 mm, rot=180:
#   pad1 -> x=70.5 mm (faces U2)
#   pad2 -> x=69.5 mm (faces the existing connector-side routes)

MCU_NETS = ('USB_D-_LINK_MCU', 'USB_D+_LINK_MCU')


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


def ref_marker(text, ref):
    markers = (
        f'(property "Reference" "{ref}"',
        f'(fp_text reference "{ref}"',
    )
    hits = [(text.find(m), m) for m in markers]
    hits = [(p, m) for p, m in hits if p >= 0]
    if not hits:
        return None, -1
    p, marker = min(hits, key=lambda x: x[0])
    return marker, p


def move_ref(text, ref, x, y, rot):
    marker, m = ref_marker(text, ref)
    if m < 0:
        raise RuntimeError(f'{ref} not found')
    i = text.rfind('(footprint ', 0, m)
    if i < 0:
        raise RuntimeError(f'footprint start not found for {ref}')
    j = balanced_block(text, i)
    blk = text[i:j]
    rel = blk.find(marker)
    if rel < 0:
        raise RuntimeError(f'reference marker not found inside {ref} footprint')
    head = blk[:rel]
    tail = blk[rel:]
    head2, n = re.subn(
        r'\(at\s+-?[\d.]+\s+-?[\d.]+(?:\s+-?[\d.]+)?\)',
        f'(at {x:.3f} {y:.3f} {rot:.3f})',
        head,
        count=1,
    )
    if n != 1:
        raise RuntimeError(f'placement not found for {ref}')
    return text[:i] + head2 + tail + text[j:]


def net_ids(text):
    return {name: nid for nid, name in
            re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', text)}


IDS = net_ids(s)


def block_has_net(block, name):
    if f'(net "{name}")' in block:
        return True
    nid = IDS.get(name)
    return bool(nid and re.search(rf'\(net\s+{re.escape(nid)}(?:\s|\))', block))


def remove_mcu_copper(text, token):
    ranges = []
    pos = 0
    needle = '(' + token
    while True:
        i = text.find(needle, pos)
        if i < 0:
            break
        j = balanced_block(text, i)
        blk = text[i:j]
        if any(block_has_net(blk, n) for n in MCU_NETS):
            ranges.append((i, j))
        pos = j
    for i, j in reversed(ranges):
        text = text[:i] + text[j:]
    return text


def seg(net, layer, x1, y1, x2, y2, width=.20):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {width:.3f}) '
            f'(layer "{layer}") (net "{net}"))')


s = move_ref(s, 'R42', 70.000, 23.000, 180.0)
s = move_ref(s, 'R43', 70.000, 25.000, 180.0)
s = remove_mcu_copper(s, 'segment')
s = remove_mcu_copper(s, 'via')

r = [
    seg('USB_D-_LINK_MCU', 'F.Cu', 73.250, 23.180, 71.300, 23.180),
    seg('USB_D-_LINK_MCU', 'F.Cu', 71.300, 23.180, 70.500, 23.000),
    seg('USB_D+_LINK_MCU', 'F.Cu', 73.250, 24.450, 71.300, 24.450),
    seg('USB_D+_LINK_MCU', 'F.Cu', 71.300, 24.450, 70.500, 25.000),
    seg('USB_D-_LINK', 'F.Cu', 69.500, 23.000, 68.500, 23.000),
    seg('USB_D+_LINK', 'F.Cu', 69.500, 25.000, 68.500, 25.000),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied conservative USB MCU-side via reduction to {P}')
