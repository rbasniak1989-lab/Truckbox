from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# TruckBox Rev.B first structural pass:
# - freeze the approved Run-159 100x65 core;
# - extend board outline to 100x95 mm (new 30 mm LTE bay);
# - repurpose the former CAN2 physical port as bidirectional J1708;
# - use automotive SN65HVD1781-Q1 (LCSC C2878178) in the existing SOIC-8;
# - implement the SAE J1708 dominant/recessive interface:
#   /RE=0, DI=0, inverted UART TX drives DE, receiver always active;
# - add the per-node J1708 load/filter: 4.7k bias, 47R series, 2.2nF shunts.

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
for name in ('J1708_DE','J1708_A_INT','J1708_B_INT'):
    if name not in known:
        adds.append(f'  (net {next_id} "{name}")')
        next_id+=1
if adds:
    matches=list(re.finditer(r'^  \(net \d+ "[^"]+"\)$',s,re.M))
    if not matches:
        raise RuntimeError('J1708 net insertion point not found')
    pos=matches[-1].end()
    s=s[:pos]+'\n'+'\n'.join(adds)+s[pos:]

net_id = {name: int(idx) for idx, name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', s)}
modern = re.search(r'\(segment\b.*?\(net\s+"[^"]+"\)', s, re.S) is not None
for required in ('GND','3V3_MAIN','J1708_RX','J1708_DE','J1708_A','J1708_B','J1708_A_INT','J1708_B_INT'):
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
u4 = set_value(u4, 'SN65HVD1781QDRQ1 C2878178 J1708')
for pin, net in {
    1:'J1708_RX',       # RO -> MCU RX
    2:'GND',            # /RE low: receiver always active
    3:'J1708_DE',       # inverted UART TX drives DE
    4:'GND',            # DI fixed low: dominant state when enabled
    5:'GND',
    6:'J1708_A_INT',    # transceiver-side A before 47R/load
    7:'J1708_B_INT',    # transceiver-side B before 47R/load
    8:'3V3_MAIN',
}.items():
    u4 = pad_net(u4, pin, net)
s = s[:a] + u4 + s[b:]

a, b, r4 = find_footprint(s, 'R4')
r4 = set_value(r4, '10k J1708_DE PULLDOWN')
r4 = pad_net(r4, 1, 'GND')
r4 = pad_net(r4, 2, 'J1708_DE')
s = s[:a] + r4 + s[b:]

# ESP32-C6-MINI-1 module pad 19 = GPIO14.
# Firmware configures this pin as UART TX and inverts TX polarity.
a,b,u1=find_footprint(s,'U1')
u1=assign_pad_net(u1,19,'J1708_DE')
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
    if name=='CAN2_TX_SAFE':
        drop=True
    if name=='3V3_MAIN' and same_pair(p1,p2,(63.0,51.0),(63.0,53.0)):
        drop=True
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
    if name=='J1708_B' and (
        same_pair(p1,p2,(80.0,48.85),(74.7,47.865)) or
        same_pair(p1,p2,(86.0,48.85),(80.0,48.85))
    ):
        drop=True
    if name=='J1708_A' and (
        same_pair(p1,p2,(80.0,50.15),(74.7,49.135)) or
        same_pair(p1,p2,(86.0,50.15),(80.0,50.15))
    ):
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


def fp0603(ref,val,x,y,rot,n1,n2):
    return f'''  (footprint "RevB:0603_J1708" (layer "F.Cu")
    (at {x:.3f} {y:.3f} {rot})
    (attr smd)
    (fp_text reference "{ref}" (at 0 -1.4 {rot}) (layer "F.SilkS") hide (effects (font (size 0.8 0.8) (thickness 0.1))))
    (fp_text value "{val}" (at 0 1.3 {rot}) (layer "F.Fab") (effects (font (size 0.6 0.6) (thickness 0.1))))
    (pad "1" smd roundrect (at -0.5 0 {rot}) (size 0.65 0.9) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) {net_expr(n1,True)}{(' (zone_connect 2)' if n1=='GND' else '')})
    (pad "2" smd roundrect (at 0.5 0 {rot}) (size 0.65 0.9) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) {net_expr(n2,True)}{(' (zone_connect 2)' if n2=='GND' else '')})
  )'''

load_parts=[
    fp0603('R53','47R J1708_A SERIES',83.5,50.15,0,'J1708_A_INT','J1708_A'),
    fp0603('R54','47R J1708_B SERIES',83.5,48.85,0,'J1708_B_INT','J1708_B'),
    fp0603('R55','4.7k J1708_A PULLUP',83.0,46.30,0,'J1708_A_INT','3V3_MAIN'),
    fp0603('R56','4.7k J1708_B PULLDOWN',79.0,46.30,0,'J1708_B_INT','GND'),
    fp0603('C52','2.2nF J1708_A EMI',83.0,44.80,0,'J1708_A_INT','GND'),
    fp0603('C53','2.2nF J1708_B EMI',79.0,44.80,0,'J1708_B_INT','GND'),
]
close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(load_parts)+'\n'+s[close:]

items=[
    # Existing receive path and surviving CAN1 support nets.
    seg('J1708_RX',69.300,46.595,65.500,46.500,.22),
    seg('CAN_MODE',78.000,40.595,78.000,44.405,.28,'B.Cu'),
    seg('3V3_MAIN',74.700,46.595,76.000,45.700,.30),
    via('3V3_MAIN',76.000,45.700),
    # Restore C12 feed after removing the old segment that crossed the former R4 pad.
    seg('3V3_MAIN',63.000,52.000,63.000,53.000,.30),

    # J1708 transmit control: inverted UART TX -> DE.
    # This exact B.Cu corridor already passed the Rev.B DRC in Run 205.
    seg('J1708_DE',26.750,19.370,28.000,19.370,.22),
    via('J1708_DE',28.000,19.370,.60,.30),
    seg('J1708_DE',28.000,19.370,27.250,18.000,.22,'B.Cu'),
    seg('J1708_DE',27.250,18.000,27.250,10.250,.22,'B.Cu'),
    seg('J1708_DE',27.250,10.250,30.750,6.750,.22,'B.Cu'),
    seg('J1708_DE',30.750,6.750,35.000,5.000,.22,'B.Cu'),
    seg('J1708_DE',35.000,5.000,66.250,4.500,.22,'B.Cu'),
    seg('J1708_DE',66.250,4.500,80.000,5.000,.22,'B.Cu'),
    seg('J1708_DE',80.000,5.000,93.750,9.000,.22,'B.Cu'),
    seg('J1708_DE',93.750,9.000,94.250,8.500,.22,'B.Cu'),
    seg('J1708_DE',94.250,8.500,94.000,9.250,.22,'B.Cu'),
    seg('J1708_DE',94.000,9.250,94.250,10.500,.22,'B.Cu'),
    seg('J1708_DE',94.250,10.500,94.750,31.250,.22,'B.Cu'),
    seg('J1708_DE',94.750,31.250,94.750,33.250,.22,'B.Cu'),
    seg('J1708_DE',94.750,33.250,93.750,34.500,.22,'B.Cu'),
    seg('J1708_DE',93.750,34.500,90.250,34.250,.22,'B.Cu'),
    seg('J1708_DE',90.250,34.250,78.250,45.000,.22,'B.Cu'),
    seg('J1708_DE',78.250,45.000,76.000,46.500,.22,'B.Cu'),
    seg('J1708_DE',76.000,46.500,75.250,46.000,.22,'B.Cu'),
    seg('J1708_DE',75.250,46.000,75.000,40.000,.22,'B.Cu'),
    seg('J1708_DE',75.000,40.000,69.000,44.000,.22,'B.Cu'),
    seg('J1708_DE',69.000,44.000,70.500,45.000,.22,'B.Cu'),
    seg('J1708_DE',70.500,45.000,70.750,52.250,.22,'B.Cu'),
    seg('J1708_DE',70.750,52.250,70.750,55.250,.22,'B.Cu'),
    seg('J1708_DE',70.750,55.250,70.250,55.750,.22,'B.Cu'),
    seg('J1708_DE',70.250,55.750,69.750,55.750,.22,'B.Cu'),
    seg('J1708_DE',69.750,55.750,69.250,55.250,.22,'B.Cu'),
    seg('J1708_DE',69.250,55.250,68.500,50.000,.22,'B.Cu'),
    seg('J1708_DE',68.500,50.000,67.000,49.250,.22,'B.Cu'),
    seg('J1708_DE',67.000,49.250,67.000,49.135,.22,'B.Cu'),
    via('J1708_DE',67.000,49.135,.60,.30),
    seg('J1708_DE',67.000,49.135,69.300,49.135,.22),
    seg('J1708_DE',67.000,49.135,64.000,51.000,.22),

    # SAE J1708 load/filter on the transceiver side of the 47R series parts.
    seg('J1708_B_INT',74.700,47.865,80.000,48.850,.30),
    seg('J1708_B_INT',80.000,48.850,83.000,48.850,.30),
    seg('J1708_B',84.000,48.850,86.000,48.850,.30),
    seg('J1708_B_INT',78.000,48.480,78.500,46.300,.22),
    seg('J1708_B_INT',78.500,46.300,78.500,44.800,.22),
    seg('GND',79.500,46.300,80.100,46.300,.25),
    via('GND',80.100,46.300,.60,.30),
    seg('GND',79.500,44.800,80.100,44.800,.25),
    via('GND',80.100,44.800,.60,.30),

    seg('J1708_A_INT',74.700,49.135,80.000,50.150,.30),
    seg('J1708_A_INT',80.000,50.150,83.000,50.150,.30),
    seg('J1708_A',84.000,50.150,86.000,50.150,.30),
    via('J1708_A_INT',81.500,50.150,.60,.30),
    seg('J1708_A_INT',81.500,50.150,82.500,47.200,.22,'B.Cu'),
    via('J1708_A_INT',82.500,47.200,.60,.30),
    seg('J1708_A_INT',82.500,47.200,82.500,46.300,.22),
    seg('J1708_A_INT',82.500,46.300,82.500,44.800,.22),
    seg('3V3_MAIN',83.500,46.300,84.100,46.300,.30),
    via('3V3_MAIN',84.100,46.300,.70,.35),
    seg('GND',83.500,44.800,84.100,44.800,.25),
    via('GND',84.100,44.800,.60,.30),
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
for needle in ('SN65HVD1781QDRQ1 C2878178 J1708','J1708_A_INT','J1708_B_INT','J1708_RX','J1708_DE'):
    if needle not in u4chk:
        raise RuntimeError(f'U4 postcondition missing {needle}')
_,_,u1chk=find_footprint(s,'U1')
if 'J1708_DE' not in u1chk:
    raise RuntimeError('U1 J1708 transmit control missing')
for ref in ('R53','R54','R55','R56','C52','C53'):
    if f'reference "{ref}"' not in s and f'(property "Reference" "{ref}"' not in s:
        raise RuntimeError(f'J1708 load component missing {ref}')
_,_,j4chk=find_footprint(s,'J4')
if 'J1708_A' not in j4chk or 'J1708_B' not in j4chk:
    raise RuntimeError('J4 J1708 remap failed')
if 'CAN2_H' in s or 'CAN2_L' in s or 'CAN2_RX' in s:
    raise RuntimeError('legacy CAN2 signal name remains')

P.write_text(s,encoding='utf-8')
print(f'Applied Rev.B 100x95 + bidirectional SAE J1708 conversion to {P}')
