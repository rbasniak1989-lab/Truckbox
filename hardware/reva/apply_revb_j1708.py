from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# TruckBox Rev.B first structural pass:
# - freeze the approved Run-159 100x65 core;
# - extend board outline to 100x95 mm (new 30 mm LTE bay);
# - repurpose the former CAN2 physical port as bidirectional J1708;
# - reuse U4 SOIC-8 with a MAX3485/SP3485-compatible 3.3-V transceiver.
# /RE stays low so the receiver remains active during transmission; the MCU
# drives DI and DE independently, allowing echo/collision monitoring.
# A hardware pull-down on DE guarantees silent power-up on the multi-master bus.

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
    for a, b, blk in iter_blocks(text, 'footprint'):
        if ref_in_footprint(blk, ref):
            return a, b, blk
    raise RuntimeError(f'footprint {ref} not found')

for old, new in (
    ('CAN2_RX', 'J1708_RX'),
    ('CAN2_H', 'J1708_B'),
    ('CAN2_L', 'J1708_A'),
):
    s = s.replace(old, new)

net_pairs=[(int(idx),name) for idx,name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)',s)]
if not net_pairs:
    raise RuntimeError('expected numeric net table before Rev.B J1708 conversion')
known={name for _,name in net_pairs}
next_id=max(idx for idx,_ in net_pairs)+1
adds=[]
for name in ('J1708_TX','J1708_DE'):
    if name not in known:
        adds.append(f'  (net {next_id} "{name}")')
        next_id+=1
if adds:
    matches=list(re.finditer(r'^  \(net \d+ "[^"]+"\)$',s,re.M))
    if not matches:
        raise RuntimeError('J1708 TX/DE net insertion point not found')
    pos=matches[-1].end()
    s=s[:pos]+'\n'+'\n'.join(adds)+s[pos:]

net_id = {name: int(idx) for idx, name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', s)}
modern = re.search(r'\(segment\b.*?\(net\s+"[^"]+"\)', s, re.S) is not None
for required in ('GND','3V3_MAIN','J1708_RX','J1708_TX','J1708_DE','J1708_A','J1708_B'):
    if required not in net_id and not modern:
        raise RuntimeError(f'net {required} not found')

def net_expr(name, for_pad=False):
    if modern:
        return f'(net "{name}")'
    if for_pad:
        return f'(net {net_id[name]} "{name}")'
    return f'(net {net_id[name]})'

def pad_net(block, padnum, name):
    m = re.search(r'\(pad\s+"?' + re.escape(str(padnum)) + r'"?\s+', block)
    if not m:
        raise RuntimeError(f'pad {padnum} not found')
    j = balanced_block(block, m.start())
    pb = block[m.start():j]
    pb2, n = re.subn(r'\(net\s+(?:\d+\s+)?"[^"]+"\)', net_expr(name, for_pad=True), pb, count=1)
    if n != 1:
        raise RuntimeError(f'pad {padnum} net field not found')
    return block[:m.start()] + pb2 + block[j:]

def assign_pad_net(block, padnum, name):
    m = re.search(r'\(pad\s+"?' + re.escape(str(padnum)) + r'"?\s+', block)
    if not m:
        raise RuntimeError(f'pad {padnum} not found')
    j = balanced_block(block, m.start())
    pb = block[m.start():j]
    if re.search(r'\(net\s+(?:\d+\s+)?"[^"]+"\)', pb):
        pb = re.sub(r'\(net\s+(?:\d+\s+)?"[^"]+"\)', net_expr(name, for_pad=True), pb, count=1)
    else:
        pb = pb[:-1] + ' ' + net_expr(name, for_pad=True) + ')'
    return block[:m.start()] + pb + block[j:]

def set_value(block, value):
    if '(property "Value"' in block:
        return re.sub(r'(\(property\s+"Value"\s+")[^"]+("\s*)',
                      lambda m: m.group(1) + value + m.group(2), block, count=1)
    return re.sub(r'(\(fp_text\s+value\s+")[^"]+("\s*)',
                  lambda m: m.group(1) + value + m.group(2), block, count=1)

a, b, u4 = find_footprint(s, 'U4')
u4 = set_value(u4, 'SP3485EEN C668205 J1708 BIDIR')
for pin, net in {
    1:'J1708_RX',   # RO
    2:'GND',        # /RE held low: receiver always active
    3:'J1708_DE',   # DE
    4:'J1708_TX',   # DI
    5:'GND',
    6:'J1708_A',
    7:'J1708_B',
    8:'3V3_MAIN',
}.items():
    u4 = pad_net(u4, pin, net)
s = s[:a] + u4 + s[b:]

a, b, r4 = find_footprint(s, 'R4')
r4 = set_value(r4, '10k J1708_RX PULLUP')
r4 = pad_net(r4, 1, '3V3_MAIN')
r4 = pad_net(r4, 2, 'J1708_RX')
s = s[:a] + r4 + s[b:]

# ESP32-C6 free module pads: 12=GPIO11 (UART TX), 13=GPIO12 (DE).
# Using the left-side free pads lets both new signals enter an internal layer
# without disturbing the frozen Rev.A routing on the right side of U1.
a,b,u1=find_footprint(s,'U1')
u1=assign_pad_net(u1,12,'J1708_TX')
u1=assign_pad_net(u1,13,'J1708_DE')
s=s[:a]+u1+s[b:]

new_ranges = []
for a, b, blk in iter_blocks(s, 'segment'):
    has_old = ('(net "CAN2_TX_SAFE")' in blk)
    if not modern and 'CAN2_TX_SAFE' in net_id:
        has_old = has_old or re.search(r'\(net\s+' + str(net_id['CAN2_TX_SAFE']) + r'\)', blk) is not None
    if has_old:
        nb = re.sub(r'\(net\s+(?:\d+\s+)?"CAN2_TX_SAFE"\)', net_expr('J1708_RX'), blk)
        if nb == blk and not modern:
            nb = re.sub(r'\(net\s+' + str(net_id['CAN2_TX_SAFE']) + r'\)', net_expr('J1708_RX'), blk)
        new_ranges.append((a,b,nb))
for a,b,nb in reversed(new_ranges):
    s = s[:a] + nb + s[b:]

def move_footprint(text, ref, x, y, rot=None):
    a,b,blk=find_footprint(text,ref)
    m=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)(?:\s+([-+0-9.]+))?\)',blk)
    if not m:
        raise RuntimeError(f'footprint {ref} has no at()')
    old_rot=float(m.group(3) or 0)
    nr=old_rot if rot is None else rot
    nb=blk[:m.start()]+f'(at {x:.3f} {y:.3f} {nr:g})'+blk[m.end():]
    return text[:a]+nb+text[b:]

# In Rev.B, U4 pin 4 is no longer GND; move C2 slightly downward to create
# a real manufacturing escape channel for J1708_TX without touching Rev.A.
s=move_footprint(s,'C2',67.500,53.600,90)

def seg_info(blk):
    ms = re.search(r'\(start\s+([-+0-9.]+)\s+([-+0-9.]+)\)', blk)
    me = re.search(r'\(end\s+([-+0-9.]+)\s+([-+0-9.]+)\)', blk)
    if not (ms and me):
        return None
    mn = re.search(r'\(net\s+"([^"]+)"\)', blk)
    if mn:
        name = mn.group(1)
    else:
        mi = re.search(r'\(net\s+(\d+)\)', blk)
        inv = {v:k for k,v in net_id.items()}
        name = inv.get(int(mi.group(1))) if mi else None
    return name, tuple(map(float,ms.groups())), tuple(map(float,me.groups()))

def same_pair(p1,p2,a,b,tol=.015):
    def close(p,q):
        return abs(p[0]-q[0])<tol and abs(p[1]-q[1])<tol
    return (close(p1,a) and close(p2,b)) or (close(p1,b) and close(p2,a))

remove=[]
for a,b,blk in iter_blocks(s,'segment'):
    inf=seg_info(blk)
    if not inf:
        continue
    name,p1,p2=inf
    drop=False
    if name=='3V3_MAIN' and same_pair(p1,p2,(69.3,49.135),(68.0,48.9)):
        drop=True
    if name=='J1708_RX' and same_pair(p1,p2,(69.3,50.405),(69.3,52.2)):
        drop=True
    if name=='J1708_RX' and same_pair(p1,p2,(64.5,52.2),(69.3,52.2)):
        drop=True
    if name=='J1708_RX' and same_pair(p1,p2,(64.5,46.5),(64.5,52.2)):
        drop=True
    if same_pair(p1,p2,(65.5,46.5),(64.5,46.5)):
        drop=True
    if name=='CAN_MODE' and (
        same_pair(p1,p2,(74.7,50.405),(76.5,50.405)) or
        same_pair(p1,p2,(76.5,50.405),(78.0,50.405)) or
        same_pair(p1,p2,(74.7,46.595),(76.5,46.595)) or
        same_pair(p1,p2,(76.5,46.595),(78.0,46.595))
    ):
        drop=True
    # Geometry fallback: remove the old CAN_MODE vertical continuation to U4
    # even if a previous KiCad normalization changed how the net is serialized.
    if abs(p1[0]-78.0)<.015 and abs(p2[0]-78.0)<.015:
        ymin=min(p1[1],p2[1]); ymax=max(p1[1],p2[1])
        if ymin <= 40.610 and ymax >= 50.390:
            drop=True
    if drop:
        remove.append((a,b))
for a,b in reversed(remove):
    s=s[:a]+s[b:]

remove=[]
for a,b,blk in iter_blocks(s,'via'):
    ma=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)\)',blk)
    if not ma:
        continue
    xy=tuple(map(float,ma.groups()))
    if any(abs(xy[0]-x)<.015 and abs(xy[1]-y)<.015 for x,y in [
        (69.3,52.2),(76.5,50.405),(76.5,46.595),(68.0,48.9),(65.5,46.5)
    ]):
        remove.append((a,b))
for a,b in reversed(remove):
    s=s[:a]+s[b:]

def seg(name,x1,y1,x2,y2,w=.25,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") {net_expr(name)})')

def via(name,x,y,size=.70,drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") {net_expr(name)})')

# Dedicated fail-safe DE pull-down. Keep the original R4/RX geometry untouched.
r52=f'''  (footprint "RevB:0603_J1708_DE_PD" (layer "F.Cu")
    (at 12.000 70.000)
    (attr smd)
    (fp_text reference "R52" (at 0 -1.5) (layer "F.SilkS") hide (effects (font (size 0.8 0.8) (thickness 0.1))))
    (fp_text value "10k J1708_DE PULLDOWN" (at 0 1.4) (layer "F.Fab") (effects (font (size 0.6 0.6) (thickness 0.1))))
    (pad "1" smd roundrect (at -0.5 0) (size 0.65 0.9) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) {net_expr('GND',True)} (zone_connect 2))
    (pad "2" smd roundrect (at 0.5 0) (size 0.65 0.9) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) {net_expr('J1708_DE',True)})
  )'''
close=s.rfind(')')
s=s[:close]+'\n'+r52+'\n'+s[close:]

items=[
    # C2 moved +1.1 mm in Y; stitch its VIN pad back to the frozen Rev.A node.
    seg('VIN_PROT',67.500,55.075,67.500,53.975,.40),
    seg('J1708_RX',69.300,46.595,65.500,46.500,.22),
    seg('CAN_MODE',78.000,40.595,78.000,44.405,.28,'B.Cu'),
    seg('3V3_MAIN',74.700,46.595,76.000,45.700,.30),
    via('3V3_MAIN',76.000,45.700),

    # TX: GPIO11 (U1 pad12) -> In2.Cu down a verified clear corridor.
    seg('J1708_TX',9.250,21.910,8.200,21.910,.22),
    via('J1708_TX',8.200,21.910,.60,.30),
    seg('J1708_TX',8.200,21.910,7.800,22.500,.22,'In2.Cu'),
    seg('J1708_TX',7.800,22.500,7.800,67.000,.22,'In2.Cu'),
    seg('J1708_TX',7.800,67.000,67.800,67.000,.22,'In2.Cu'),
    seg('J1708_TX',67.800,67.000,67.800,50.405,.22,'In2.Cu'),
    via('J1708_TX',67.800,50.405,.60,.30),
    seg('J1708_TX',67.800,50.405,69.300,50.405,.22),

    # DE: GPIO12 (U1 pad13) -> independent In2.Cu corridor.
    seg('J1708_DE',9.250,23.180,8.800,23.180,.22),
    via('J1708_DE',8.800,23.180,.60,.30),
    seg('J1708_DE',8.800,23.180,10.800,25.000,.22,'In2.Cu'),
    seg('J1708_DE',10.800,25.000,10.800,65.500,.22,'In2.Cu'),
    seg('J1708_DE',10.800,65.500,67.000,65.500,.22,'In2.Cu'),
    seg('J1708_DE',67.000,65.500,67.000,49.135,.22,'In2.Cu'),
    via('J1708_DE',67.000,49.135,.60,.30),
    seg('J1708_DE',67.000,49.135,69.300,49.135,.22),

    # R52 fail-safe pull-down in LTE bay; pad1 returns through the F.Cu GND pour.
    via('J1708_DE',12.500,65.500,.60,.30),
    seg('J1708_DE',12.500,65.500,12.500,70.000,.22),
    seg('GND',11.500,70.000,10.500,70.000,.25),
    via('GND',10.500,70.000,.60,.30),
]
close=s.rfind(')')
if close<0:
    raise RuntimeError('board closing paren not found')
s=s[:close]+'\n'+'\n'.join(items)+'\n'+s[close:]

edge_repl=[]
for a,b,blk in iter_blocks(s,'gr_line'):
    if 'Edge.Cuts' not in blk:
        continue
    st=re.search(r'\(start\s+([-+0-9.]+)\s+([-+0-9.]+)\)',blk)
    en=re.search(r'\(end\s+([-+0-9.]+)\s+([-+0-9.]+)\)',blk)
    if not(st and en):
        continue
    x1,y1=map(float,st.groups())
    x2,y2=map(float,en.groups())
    nb=blk
    if abs(y1-65)<.01:
        nb=re.sub(r'(\(start\s+[-+0-9.]+\s+)65(?:\.0+)?(\))',r'\g<1>95\2',nb,count=1)
    if abs(y2-65)<.01:
        nb=re.sub(r'(\(end\s+[-+0-9.]+\s+)65(?:\.0+)?(\))',r'\g<1>95\2',nb,count=1)
    edge_repl.append((a,b,nb))
for a,b,nb in reversed(edge_repl):
    s=s[:a]+nb+s[b:]

s=s.replace('TruckBox Rev.A | 100x65 | 4L | PROTOTYPE',
            'TruckBox Rev.B | 100x95 | J1939 + J1708 + LTE')

_,_,u4chk=find_footprint(s,'U4')
for needle in ('SP3485EEN C668205 J1708 BIDIR','J1708_A','J1708_B','J1708_RX','J1708_TX','J1708_DE'):
    if needle not in u4chk:
        raise RuntimeError(f'U4 postcondition missing {needle}')
_,_,u1chk=find_footprint(s,'U1')
for needle in ('J1708_TX','J1708_DE'):
    if needle not in u1chk:
        raise RuntimeError(f'U1 postcondition missing {needle}')
if 'reference "R52"' not in s and '(property "Reference" "R52"' not in s:
    raise RuntimeError('R52 DE pull-down missing')
_,_,j4chk=find_footprint(s,'J4')
if 'J1708_A' not in j4chk or 'J1708_B' not in j4chk:
    raise RuntimeError('J4 J1708 remap failed')
if 'CAN2_H' in s or 'CAN2_L' in s or 'CAN2_RX' in s:
    raise RuntimeError('legacy CAN2 signal name remains')

P.write_text(s,encoding='utf-8')
print(f'Applied Rev.B 100x95 + bidirectional J1708 conversion to {P}')
