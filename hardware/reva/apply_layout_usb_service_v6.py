from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', s)}
id_net = {i: name for name, i in net_id.items()}


def balanced_block(text, start):
    depth = 0; in_q = False; esc = False
    for j in range(start, len(text)):
        c = text[j]
        if in_q:
            if esc: esc = False
            elif c == '\\': esc = True
            elif c == '"': in_q = False
            continue
        if c == '"': in_q = True
        elif c == '(': depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0: return j + 1
    raise RuntimeError('unterminated s-expression')


def block_net(b):
    m = re.search(r'\(net(?:\s+\d+)?\s+"([^"]+)"\)', b)
    if m: return m.group(1)
    m = re.search(r'\(net\s+(\d+)\)', b)
    return id_net.get(int(m.group(1))) if m else None


def block_layer(b):
    m = re.search(r'\(layer\s+"([^"]+)"\)', b)
    return m.group(1) if m else None


def block_points(b):
    return [(float(x), float(y)) for x, y in re.findall(
        r'\((?:start|end|at)\s+([-+0-9.]+)\s+([-+0-9.]+)', b)]


def has_point(b, x, y, tol=.012):
    return any(abs(px-x) <= tol and abs(py-y) <= tol for px, py in block_points(b))


def remove_blocks(text, token, pred):
    ranges=[]; pos=0; needle='('+token
    while True:
        i=text.find(needle,pos)
        if i<0: break
        j=balanced_block(text,i); b=text[i:j]
        if pred(b): ranges.append((i,j))
        pos=j
    for i,j in reversed(ranges): text=text[:i]+text[j:]
    return text


def ref_marker_pos(text, ref):
    hits=[]
    for marker in (f'(property "Reference" "{ref}"', f'(fp_text reference "{ref}"'):
        p=text.find(marker)
        if p>=0: hits.append(p)
    return min(hits) if hits else -1


def shrink_u2_right_courtyard(text, delta=.35):
    """Reduce only U2's right F.CrtYd edge, independent of KiCad formatting."""
    m=ref_marker_pos(text,'U2')
    if m<0: raise RuntimeError('U2 not found')
    i=text.rfind('(footprint',0,m)
    j=balanced_block(text,i)
    b=text[i:j]
    pos=0
    while True:
        k=b.find('(fp_rect',pos)
        if k<0: break
        e=balanced_block(b,k)
        blk=b[k:e]
        if '(layer "F.CrtYd")' in blk:
            mm=re.search(r'\(end\s+([-+0-9.]+)\s+([-+0-9.]+)\)',blk)
            if not mm: raise RuntimeError('U2 F.CrtYd end not found')
            old_x=float(mm.group(1)); y=mm.group(2); new_x=old_x-delta
            repl=f'(end {new_x:.3f} {y})'
            blk2=blk[:mm.start()]+repl+blk[mm.end():]
            b2=b[:k]+blk2+b[e:]
            return text[:i]+b2+text[j:]
        pos=e
    raise RuntimeError('U2 F.CrtYd rectangle not found')


def seg(net,layer,x1,y1,x2,y2,w=.20):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "{net}"))')


# 1) LINK_BOOT: run-138 showed both vias were the source of ten separate
# clearance/hole-clearance violations. The pad-to-R30 connection has a clean
# all-F.Cu corridor above J3, so remove the whole old net and use no vias.
s=remove_blocks(s,'segment',lambda b:block_net(b)=='LINK_BOOT')
s=remove_blocks(s,'via',lambda b:block_net(b)=='LINK_BOOT')

# 2) CAN_MODE: only replace the B.Cu x=66.5..78, y=30 crossbar. D- occupies
# x=72.8 on B.Cu from y=24..32.2; the new y=33 dogleg clears it by >0.38 mm.
s=remove_blocks(
    s,'segment',
    lambda b: block_net(b)=='CAN_MODE' and block_layer(b)=='B.Cu'
    and has_point(b,66.5,30.0) and has_point(b,78.0,30.0)
)

# 3) U2/J3: J3's connector datum is intentionally aligned to the 100-mm board
# edge, so do not move J3. Reduce only U2's right courtyard edge by 0.35 mm;
# component copper/pads and physical placement remain unchanged.
s=shrink_u2_right_courtyard(s,.35)

r=[
    # LINK_BOOT from U2 pin 15 to R30 pad 2. First move up/right away from U2
    # pin 16 (GNSS_PPS), then traverse above the USB-C shell stake.
    seg('LINK_BOOT','F.Cu',90.75,24.45,91.50,23.50,.20),
    seg('LINK_BOOT','F.Cu',91.50,23.50,91.50,22.00,.20),
    seg('LINK_BOOT','F.Cu',91.50,22.00,94.50,22.00,.20),
    seg('LINK_BOOT','F.Cu',94.50,22.00,94.50,20.50,.20),

    # CAN_MODE B.Cu dogleg below the D- vertical bridge.
    seg('CAN_MODE','B.Cu',66.50,30.00,66.50,33.00,.28),
    seg('CAN_MODE','B.Cu',66.50,33.00,78.00,33.00,.28),
    seg('CAN_MODE','B.Cu',78.00,33.00,78.00,30.00,.28),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied USB-C final local cleanup v6 to {P}')
