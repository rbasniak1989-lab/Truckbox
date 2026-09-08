from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.A v4: KiCad's board coordinate system makes positive footprint rotation
# appear clockwise on screen.  v3 exposed a few reversed pad assumptions.
# This pass uses coordinates observed in the KiCad 9 DRC report.

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
                    if depth==0: b=j+1; break
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

# Repack only items implicated by v3 DRC.  D1 is lowered and shifted left so
# its courtyard clears J4; D2 moves below CAN2; C3/C1/C2 are flipped so VIN is
# on the side used by the protected rail; C4 moves below U6 so BOOT never has
# to cross VIN.
moves={
    'D1':(83.5,60.0,180),
    'D2':(82.0,53.0,180),
    'C3':(70.0,59.5,90),
    'C1':(63.5,57.0,270),
    'C2':(67.5,52.5,270),
    'D5':(56.2,52.5,270),
    'C4':(58.0,61.5,180),
    'C7':(43.0,55.5,180),
    'C8':(43.0,60.5,180),
    'R31':(55.0,47.0,0),
    'R32':(58.0,47.0,0),
    'R9':(61.0,47.0,0),
    'C6':(64.0,47.0,0),
    'R4':(63.5,51.0,0),
    'C12':(63.5,54.0,0),
    'R8':(54.5,63.0,0),
    'C5':(62.0,63.0,0),
    'C13':(36.0,40.0,0),
}
for ref,(x,y,r) in moves.items():
    s=edit_ref(s,ref,lambda b,x=x,y=y,r=r:set_at(b,x,y,r))

# Temporary prototype legend will be recreated after routing; remove it from
# the critical DRC passes so it cannot generate false silk/copper warnings.
s='\n'.join(line for line in s.splitlines()
            if 'gr_text "TruckBox Rev.A | 100x65 | 4L | PROTOTYPE"' not in line)+'\n'

# Replace the v3 critical routes.  No low-speed routing exists yet, so this is
# safe and keeps every iteration deterministic.
s='\n'.join(line for line in s.splitlines()
            if not line.startswith('  (segment ') and not line.startswith('  (via '))+'\n'

def seg(netid,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {netid}))')

r=[]

# CAN1 / CAN2.  D2 now sits at y=53, below the CAN2 route.
r += [
    seg(16,94.2,43.0,91.0,43.0,.30),
    seg(16,91.0,43.0,88.5,41.85,.30),
    seg(16,88.5,41.85,86.0,41.85,.30),
    seg(17,94.2,45.5,91.2,45.5,.30),
    seg(17,91.2,45.5,88.5,43.15,.30),
    seg(17,88.5,43.15,86.0,43.15,.30),
    seg(16,86.0,41.85,74.7,41.865,.30),
    seg(17,86.0,43.15,74.7,43.135,.30),
    seg(20,94.2,48.0,91.0,48.0,.30),
    seg(20,91.0,48.0,88.5,48.85,.30),
    seg(20,88.5,48.85,86.0,48.85,.30),
    seg(21,94.2,50.5,91.2,50.5,.30),
    seg(21,91.2,50.5,88.5,50.15,.30),
    seg(21,88.5,50.15,86.0,50.15,.30),
    seg(20,86.0,48.85,80.0,48.85,.30),
    seg(20,80.0,48.85,74.7,47.865,.30),
    seg(21,86.0,50.15,80.0,50.15,.30),
    seg(21,80.0,50.15,74.7,49.135,.30),
]

# GNSS RF unchanged; it was already clean in v2/v3.
r += [
    seg(52,45.15,13.7,42.5,13.7,.40),
    seg(52,42.5,13.7,37.95,13.0,.40),
]

# 24V rail.  D2 pads are y=53: pin1/BATT x84.35, pin2/VIN x79.65.
# D1 at x83.5/y60 rotated180: VIN pad2 x77.5, GND pad1 x89.5.
# C3 at x70/y59.5 rotated90: VIN pad1 is the upper pad y56.4.
r += [
    seg(2,97.8,53.0,84.35,53.0,.80),
    seg(3,79.65,53.0,79.65,56.0,.80),
    seg(3,79.65,56.0,77.5,60.0,.80),
    seg(3,77.5,60.0,75.0,56.4,.80),
    seg(3,75.0,56.4,70.0,56.4,.80),
    # Bulk VIN -> C1 VIN -> U6 VIN.  C1 rot270 gives VIN at y55.9.
    seg(3,70.0,56.4,66.0,55.9,.60),
    seg(3,66.0,55.9,63.5,55.9,.60),
    seg(3,63.5,55.9,62.0,55.9,.45),
    seg(3,62.0,55.9,62.0,58.5,.35),
    seg(3,62.0,58.5,60.15,58.5,.30),
]

# Main PH -> L1 escape.  p10 is y59, p9 is y58.5; 0.25 mm clears the pin.
r += [
    seg(7,55.85,59.0,54.8,59.0,.25),
    seg(7,54.8,59.0,53.5,59.0,.55),
    # Catch diode D5 rot270: SW pad2 is the lower pad y54.9.
    seg(7,53.5,59.0,53.5,56.0,.40),
    seg(7,53.5,56.0,56.2,54.9,.40),
    # Bootstrap cap below U6, C4 rot180: SW pad2 x57.5, BOOT pad1 x58.5.
    seg(7,55.85,59.0,56.0,60.0,.25),
    seg(7,56.0,60.0,57.5,61.5,.25),
    seg(8,60.15,59.0,60.0,60.1,.25),
    seg(8,60.0,60.1,58.5,61.5,.25),
]

# 3V3: C7/C8 are rotated 180 so their 3V3 pad1 faces the inductor/right side.
r += [
    seg(9,47.5,59.0,46.0,59.0,.80),
    seg(9,46.0,59.0,46.0,55.5,.60),
    seg(9,46.0,55.5,44.1,55.5,.60),
    seg(9,46.0,59.0,46.0,60.5,.60),
    seg(9,46.0,60.5,44.1,60.5,.60),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v4 pad-orientation cleanup to {P}')
