from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

# BUCK_SS endpoint geometry:
# U6 pin 4 = (60.15,57.50), C5 pad 1 = (61.50,63.00).
# The cleanest breakout is to keep C1 in its original courtyard and flip its
# orientation so the VIN pad faces U6. This shortens the high-current VIN loop,
# removes the original x=61.4 dogleg that blocked SS, and avoids courtyard moves.

def blocks(text, token='  (footprint '):
    out=[]; i=0
    while True:
        a=text.find(token,i)
        if a<0: break
        depth=0; ins=False; esc=False; b=None
        for j in range(a,len(text)):
            c=text[j]
            if ins:
                if esc: esc=False
                elif c=='\\': esc=True
                elif c=='"': ins=False
            else:
                if c=='"': ins=True
                elif c=='(': depth+=1
                elif c==')':
                    depth-=1
                    if depth==0:
                        b=j+1; break
        if b is None: raise RuntimeError('unbalanced footprint')
        out.append((a,b,text[a:b])); i=b
    return out

def ref_of(blk):
    m=re.search(r'\(fp_text reference "([^"]+)"',blk)
    return m.group(1) if m else None

def edit_ref(text,ref,fn):
    for a,b,blk in blocks(text):
        if ref_of(blk)==ref:
            return text[:a]+fn(blk)+text[b:]
    raise RuntimeError(f'footprint {ref} not found')

def set_at(blk,x,y,rot=0):
    return re.sub(r'\n    \(at [^\n]+\)',f'\n    (at {x:.3f} {y:.3f} {rot})',blk,count=1)

# Keep C1 at the original center, flip it so VIN pad 1 is at (63.5,58.475)
# and GND pad 2 is at (63.5,55.525).
s = edit_ref(s,'C1',lambda b:set_at(b,63.5,57.0,90))

# Remove the old C3->C1 and C1->U6 VIN approach.
old_vin = [
    '  (segment (start 66.500 56.400) (end 63.500 55.525) (width 0.450) (layer "F.Cu") (net 3))',
    '  (segment (start 63.500 55.525) (end 62.200 56.200) (width 0.350) (layer "F.Cu") (net 3))',
    '  (segment (start 62.200 56.200) (end 61.400 57.200) (width 0.280) (layer "F.Cu") (net 3))',
    '  (segment (start 61.400 57.200) (end 61.400 58.500) (width 0.220) (layer "F.Cu") (net 3))',
    '  (segment (start 61.400 58.500) (end 60.150 58.500) (width 0.200) (layer "F.Cu") (net 3))',
]
for line in old_vin:
    if line not in s:
        raise RuntimeError(f'expected VIN_PROT segment missing: {line}')
    s = s.replace(line + '\n', '', 1)

# Remove any previous BUCK_SS route so this pass is idempotent.
ss_id = net_id['BUCK_SS']
new = []
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m = re.search(r'\(net (\d+)\)', line)
        if m and int(m.group(1)) == ss_id:
            continue
    new.append(line)
s = '\n'.join(new) + '\n'

def seg(net, x1, y1, x2, y2, w=.22, layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {net_id[net]}))')

def via(net, x, y, size=.70, drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") (net {net_id[net]}))')

r = [
    # C3 -> flipped C1 VIN pad, then a short direct run into U6 VIN/pin2.
    # C1 pad 1 copper spans roughly x=62.15..64.85 at y=58.475, so the
    # 0.20-mm segment exits from its left edge and stays clear of BUCK_SS.
    seg('VIN_PROT', 66.50, 56.40, 63.50, 58.475, .35),
    seg('VIN_PROT', 63.50, 58.475, 60.15, 58.50, .20),

    # Soft-start: escape laterally beyond adjacent TPS pads, then use B.Cu.
    # x=61.30 keeps the vias clear of both adjacent U6 pins and BUCK_RT x=62.
    seg('BUCK_SS', 60.15, 57.50, 61.30, 57.50, .20),
    via('BUCK_SS', 61.30, 57.50),
    seg('BUCK_SS', 61.30, 57.50, 61.30, 61.50, .22, 'B.Cu'),
    via('BUCK_SS', 61.30, 61.50),
    seg('BUCK_SS', 61.30, 61.50, 61.30, 63.00, .22),
    seg('BUCK_SS', 61.30, 63.00, 61.50, 63.00, .22),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied BUCK_SS routing + flipped C1 VIN pass to {P}')
