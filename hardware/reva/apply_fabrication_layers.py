from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Final manufacturing pass for Rev.A:
#   1) replace the prototype J4 pigtail pads with an SMT Molex Micro-Fit 3.0
#      8-way header (430450818 / JLCPCB C505075);
#   2) make the minimum local CAN-protection placement/routing adjustment needed
#      for connector clearance; everything else remains frozen;
#   3) declare the standard solder-mask layers for fabrication export.
#
# J4 harness circuits after this pass (Molex circuit number -> signal):
#   1 NC/RESERVE, 2 ACC, 3 GND, 4 CAN2_H,
#   5 CAN1_H, 6 CAN1_L, 7 CAN2_L, 8 +24V.
# The header is centered at (93.5,48) and rotated 90 deg so the 11.2 mm body
# depth fits at the right board edge. D3/D4 move only 1.5 mm left to preserve
# copper/courtyard clearance from J4.


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
    # The first (at ...) in a footprint is the footprint origin.
    nb, n = re.subn(
        r'\(at\s+[-+0-9.]+\s+[-+0-9.]+(?:\s+[-+0-9.]+)?\)',
        f'(at {x:.3f} {y:.3f} {rot})',
        block,
        count=1,
    )
    if n != 1:
        raise RuntimeError(f'could not move footprint {ref}')
    return text[:i] + nb + text[j:]


# Net table is present in both the hand-generated input and KiCad-normalized
# board. Use it so the same patch is deterministic in CI and easy to replay on
# a saved board during review.
net_id = {name: int(idx) for idx, name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', s)}
modern_net_syntax = re.search(r'\(segment\b.*?\(net\s+"[^"]+"\)', s, re.S) is not None
if not modern_net_syntax:
    for required in ('GND','BATT24_FUSED','ACC_RAW','CAN1_H','CAN1_L','CAN2_H','CAN2_L'):
        if required not in net_id:
            raise RuntimeError(f'net {required} not found')


def net_expr(name, for_pad=False):
    if modern_net_syntax:
        return f'(net "{name}")'
    if for_pad:
        return f'(net {net_id[name]} "{name}")'
    return f'(net {net_id[name]})'


# --- J4: Molex Micro-Fit 3.0 430450818, vertical SMT, 2x4, 3.00 mm pitch ---
# Signal-pad centers/sizes and the two metal hold-down pads follow the standard
# KiCad/Molex 43045-0818 footprint. J4 is rotated 90 degrees on the board.
def j4_pad(num, x, y, net=None, pin1=False):
    shape = 'rect'
    rr = ''
    ne = (' ' + net_expr(net, for_pad=True)) if net else ''
    return (
        f'    (pad "{num}" smd {shape} (at {x:.3f} {y:.3f}) '
        f'(size 1.270 2.540) (layers "F.Cu" "F.Paste" "F.Mask"){rr}{ne})'
    )

j4_lines = [
    '  (footprint "TruckBox:Molex_Micro-Fit_3.0_43045-0818_2x04-1MP_P3.00mm_Vertical" (layer "F.Cu")',
    '    (at 93.500 48.000 90)',
    '    (attr smd)',
    '    (fp_text reference "J4" (at 0 -7.17 90) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))',
    '    (fp_text value "Molex 430450818" (at 0 7.17 90) (layer "F.Fab") (effects (font (size 0.7 0.7) (thickness 0.1))))',
    '    (fp_rect (start -8.075 -3.940) (end 8.075 4.830) (stroke (width 0.10) (type solid)) (fill none) (layer "F.Fab"))',
    '    (fp_line (start -5.25 -5.95) (end -3.75 -5.95) (stroke (width 0.20) (type solid)) (layer "F.SilkS"))',
    # Standard KiCad 43045-0818 courtyard is added below after this list.
    j4_pad(1,-4.5,-4.7,None,True),
    j4_pad(2,-1.5,-4.7,'ACC_RAW'),
    j4_pad(3, 1.5,-4.7,'GND'),
    j4_pad(4, 4.5,-4.7,'CAN2_H'),
    j4_pad(5,-4.5, 4.7,'CAN1_H'),
    j4_pad(6,-1.5, 4.7,'CAN1_L'),
    j4_pad(7, 1.5, 4.7,'CAN2_L'),
    j4_pad(8, 4.5, 4.7,'BATT24_FUSED'),
    '    (pad "MP" smd rect (at -8.385 0) (size 3.430 1.650) (layers "F.Cu" "F.Paste" "F.Mask"))',
    '    (pad "MP" smd rect (at 8.385 0) (size 3.430 1.650) (layers "F.Cu" "F.Paste" "F.Mask"))',
    '  )',
]
# Exact non-rectangular courtyard from the standard KiCad/Molex 43045-0818 footprint.
crtyd = [
    (-10.6,-1.33,-10.6,1.33), (-10.6,1.33,-8.58,1.33),
    (-8.58,-4.44,-8.58,-1.33), (-8.58,-1.33,-10.6,-1.33),
    (-8.58,1.33,-8.58,3.93), (-8.58,3.93,-5.64,3.93),
    (-5.64,-6.47,-5.64,-4.44), (-5.64,-4.44,-8.58,-4.44),
    (-5.64,3.93,-5.64,6.47), (-5.64,6.47,0,6.47),
    (0,-6.47,-5.64,-6.47), (0,-6.47,5.64,-6.47),
    (5.64,-6.47,5.64,-4.44), (5.64,-4.44,8.58,-4.44),
    (5.64,3.93,5.64,6.47), (5.64,6.47,0,6.47),
    (8.58,-4.44,8.58,-1.33), (8.58,-1.33,10.6,-1.33),
    (8.58,1.33,8.58,3.93), (8.58,3.93,5.64,3.93),
    (10.6,-1.33,10.6,1.33), (10.6,1.33,8.58,1.33),
]
crtyd_lines = [
    f'    (fp_line (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))'
    for x1,y1,x2,y2 in crtyd
]
# Insert courtyard immediately before the two MP pads / closing parenthesis; ordering is not electrically significant.
j4_lines[-1:-1] = crtyd_lines
j4 = '\n'.join(j4_lines)
i, j, _old_j4 = find_footprint(s, 'J4')
s = s[:i] + j4 + s[j:]

# Move only the two CAN TVS footprints 1.5 mm left. Their orientation and pad/net
# assignment stay unchanged.
s = move_footprint(s, 'D3', 85.5, 42.5, 0)
s = move_footprint(s, 'D4', 85.5, 49.5, 0)

# Remove the old connector-side CAN fanout. Keep the transceiver-side segments
# (they extend left of x=85.9) and reconnect them below.
def segment_info(block):
    ms = re.search(r'\(start\s+([-+0-9.]+)\s+([-+0-9.]+)\)', block)
    me = re.search(r'\(end\s+([-+0-9.]+)\s+([-+0-9.]+)\)', block)
    if not (ms and me):
        return None
    mn = re.search(r'\(net\s+"([^"]+)"\)', block)
    if mn:
        name = mn.group(1)
    else:
        mi = re.search(r'\(net\s+(\d+)\)', block)
        if not mi:
            return None
        inv = {v:k for k,v in net_id.items()}
        name = inv.get(int(mi.group(1)))
    return name, tuple(map(float, ms.groups())), tuple(map(float, me.groups()))

remove_ranges = []
for a, b, block in iter_blocks(s, 'segment'):
    info = segment_info(block)
    if not info:
        continue
    name, p1, p2 = info
    drop_can = name in {'CAN1_H','CAN1_L','CAN2_H','CAN2_L'} and min(p1[0], p2[0]) >= 85.9
    drop_old_batt_j4 = (
        name == 'BATT24_FUSED'
        and {tuple(round(v,3) for v in p1), tuple(round(v,3) for v in p2)}
            == {(97.8,53.0),(90.1,53.0)}
    )
    if drop_can or drop_old_batt_j4:
        remove_ranges.append((a,b))
for a,b in reversed(remove_ranges):
    s = s[:a] + s[b:]


def seg(name, x1, y1, x2, y2, width=0.30):
    return (
        f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
        f'(width {width:.3f}) (layer "F.Cu") {net_expr(name)})'
    )

# Reconnect moved D3/D4 to the frozen transceiver-side copper.
new_segments = [
    # Reconnect moved D3/D4 to the frozen transceiver-side copper.
    seg('CAN1_H',84.5,41.85,86.0,41.85),
    seg('CAN1_L',84.5,43.15,86.0,43.15),
    seg('CAN2_H',84.5,48.85,86.0,48.85),
    seg('CAN2_L',84.5,50.15,86.0,50.15),
    # J4 -> D3/D4, short local fanout with physical separation between pairs.
    seg('CAN1_H',88.8,43.5,87.7,41.85),
    seg('CAN1_H',87.7,41.85,84.5,41.85),
    seg('CAN1_L',88.8,46.5,87.8,45.0),
    seg('CAN1_L',87.8,45.0,84.5,43.15),
    # CAN2_L stays on the inner row; this avoids the large protected-input
    # copper at D2. CAN2_H uses the outer row and approaches D4 from above.
    seg('CAN2_L',88.8,49.5,87.2,50.15),
    seg('CAN2_L',87.2,50.15,84.5,50.15),
    seg('CAN2_H',98.2,52.5,92.5,52.5),
    seg('CAN2_H',92.5,52.5,92.5,48.0),
    seg('CAN2_H',92.5,48.0,87.2,48.0),
    seg('CAN2_H',87.2,48.0,84.5,48.85),
    # +24 V pin 8 joins the existing D2/D1 protected-input branch locally.
    seg('BATT24_FUSED',88.8,52.5,90.1,53.0,1.20),
    # ACC reuses the frozen J4-side copper endpoint.
    seg('ACC_RAW',98.2,46.5,97.8,45.5,0.28),
    # J4 GND gets its own short stitch to the continuous inner GND plane so
    # CAN2_H can pass inward without crossing a long ground trace.
    seg('GND',98.2,49.5,96.0,49.5,0.60),
]
close = s.rfind(')')
if close < 0:
    raise RuntimeError('board closing paren not found')
# Dedicated GND stitch next to J4. Existing Rev.A uses the same 0.7/0.35 mm
# via geometry for local ground-island stitching.
if modern_net_syntax:
    gnd_via = '  (via (at 96.000 49.500) (size 0.700) (drill 0.350) (layers "F.Cu" "B.Cu") (net "GND"))'
else:
    gnd_via = f'  (via (at 96.000 49.500) (size 0.700) (drill 0.350) (layers "F.Cu" "B.Cu") (net {net_id["GND"]}))'
s = s[:close] + '\n' + '\n'.join(new_segments) + '\n' + gnd_via + '\n' + s[close:]

# Postconditions for the connector change.
_, _, chk_j4 = find_footprint(s, 'J4')
if '430450818' not in chk_j4 or '(at 93.500 48.000 90)' not in chk_j4:
    raise RuntimeError('J4 Micro-Fit replacement failed')
for ref, expected in [('D3','(at 85.500 42.500 0)'),('D4','(at 85.500 49.500 0)')]:
    _, _, blk = find_footprint(s, ref)
    if expected not in blk:
        raise RuntimeError(f'{ref} relocation failed')

# --- Fabrication layer declaration ---
# KiCad 9/10 current layer IDs used by this generated board:
#   1 = F.Mask
#   3 = B.Mask
# Find the top-level layer table, and inspect ONLY that table. Pad definitions
# also contain the strings F.Mask/B.Mask and must not be mistaken for enabled
# board layers.
i = s.find('(layers')
if i < 0:
    raise RuntimeError('top-level layers block not found')
j = balanced_block(s, i)
blk = s[i:j]

has_fmask = re.search(r'\(1\s+"F\.Mask"\s+user(?:\s|\))', blk) is not None
has_bmask = re.search(r'\(3\s+"B\.Mask"\s+user(?:\s|\))', blk) is not None

if not (has_fmask and has_bmask):
    im = re.search(r'\n([ \t]+)\(\d+\s+"[^"]+"', blk)
    indent = im.group(1) if im else '\t\t'
    rows = []
    if not has_fmask:
        rows.append(indent + '(1 "F.Mask" user)')
    if not has_bmask:
        rows.append(indent + '(3 "B.Mask" user)')
    close = blk.rfind(')')
    if close < 0:
        raise RuntimeError('layers block closing paren not found')
    before = blk[:close].rstrip()
    close_indent = re.search(r'\n([ \t]*)$', blk[:close])
    parent_indent = close_indent.group(1) if close_indent else '\t'
    blk = before + '\n' + '\n'.join(rows) + '\n' + parent_indent + ')'
    s = s[:i] + blk + s[j:]

# Re-read only the resulting layer table for a strict postcondition.
i2 = s.find('(layers')
j2 = balanced_block(s, i2)
blk2 = s[i2:j2]
if re.search(r'\(1\s+"F\.Mask"\s+user(?:\s|\))', blk2) is None:
    raise RuntimeError('F.Mask layer declaration missing after patch')
if re.search(r'\(3\s+"B\.Mask"\s+user(?:\s|\))', blk2) is None:
    raise RuntimeError('B.Mask layer declaration missing after patch')

P.write_text(s, encoding='utf-8')
print(f'Applied J4 Micro-Fit 430450818 + final fabrication layers in {P}')
