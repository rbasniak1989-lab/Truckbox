from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# TruckBox Rev.B first structural pass:
# - freeze the approved Run-159 100x65 core;
# - extend board outline to 100x95 mm (new 30 mm LTE bay);
# - repurpose the former CAN2 physical port as receive-only J1708;
# - reuse U4 SOIC-8 with a MAX3485-compatible 3.3-V transceiver.
# Driver is physically disabled: /RE=0, DE=0, DI=0. Only RO reaches the MCU.

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

net_id = {name: int(idx) for idx, name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', s)}
modern = re.search(r'\(segment\b.*?\(net\s+"[^"]+"\)', s, re.S) is not None
for required in ('GND','3V3_MAIN','J1708_RX','J1708_A','J1708_B'):
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

def set_value(block, value):
    if '(property "Value"' in block:
        return re.sub(r'(\(property\s+"Value"\s+")[^"]+("\s*)',
                      lambda m: m.group(1) + value + m.group(2), block, count=1)
    return re.sub(r'(\(fp_text\s+value\s+")[^"]+("\s*)',
                  lambda m: m.group(1) + value + m.group(2), block, count=1)

a, b, u4 = find_footprint(s, 'U4')
u4 = set_value(u4, 'MAX3485ESA J1708 RX ONLY')
for pin, net in {
    1:'J1708_RX',
    2:'GND',
    3:'GND',
    4:'GND',
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
    if name=='CAN_MODE' and (
        same_pair(p1,p2,(74.7,50.405),(76.5,50.405)) or
        same_pair(p1,p2,(76.5,50.405),(78.0,50.405)) or
        same_pair(p1,p2,(74.7,46.595),(76.5,46.595)) or
        same_pair(p1,p2,(76.5,46.595),(78.0,46.595))
    ):
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
        (69.3,52.2),(76.5,50.405),(76.5,46.595),(68.0,48.9)
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

items=[
    seg('J1708_RX',69.300,46.595,65.500,46.500,.22),
    seg('CAN_MODE',78.000,40.595,78.000,44.405,.28,'B.Cu'),
    seg('3V3_MAIN',74.700,46.595,76.000,45.700,.30),
    via('3V3_MAIN',76.000,45.700),
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
for needle in ('MAX3485ESA J1708 RX ONLY','J1708_A','J1708_B','J1708_RX'):
    if needle not in u4chk:
        raise RuntimeError(f'U4 postcondition missing {needle}')
_,_,j4chk=find_footprint(s,'J4')
if 'J1708_A' not in j4chk or 'J1708_B' not in j4chk:
    raise RuntimeError('J4 J1708 remap failed')
if 'CAN2_H' in s or 'CAN2_L' in s or 'CAN2_RX' in s:
    raise RuntimeError('legacy CAN2 signal name remains')

P.write_text(s,encoding='utf-8')
print(f'Applied Rev.B 100x95 + receive-only J1708 conversion to {P}')
