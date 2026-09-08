from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.A v5: replace the provisional 1210 land pattern with the IPC nominal
# geometry used by KiCad's C_1210_3225Metric footprint:
# pad centers +/-1.475 mm, pad size 1.15 x 2.70 mm, courtyard +/-2.3 x +/-1.6.

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

def fix_1210(blk):
    blk=blk.replace(
        '(fp_rect (start -1.850 -1.500) (end 1.850 1.500)',
        '(fp_rect (start -2.300 -1.600) (end 2.300 1.600)')
    blk=blk.replace(
        '(pad "1" smd roundrect (at -1.100 0.000 0) (size 1.430 2.700)',
        '(pad "1" smd roundrect (at -1.475 0.000 0) (size 1.150 2.700)')
    blk=blk.replace(
        '(pad "2" smd roundrect (at 1.100 0.000 0) (size 1.430 2.700)',
        '(pad "2" smd roundrect (at 1.475 0.000 0) (size 1.150 2.700)')
    return blk

for ref in ['C1','C2','C7','C8']:
    s=edit_ref(s,ref,fix_1210)

# C12 was only 0.1 mm from the old C1 land; move it upward.  R8 moves clear
# of the inductor courtyard while remaining inside the board edge.
s=edit_ref(s,'C12',lambda b:set_at(b,63.5,53.0,0))
s=edit_ref(s,'R8',lambda b:set_at(b,56.0,64.0,0))

# v4 routing endpoints referenced the provisional 1210 pad centers.  Rebuild
# the same critical nets with the IPC coordinates.
s='\n'.join(line for line in s.splitlines()
            if not line.startswith('  (segment ') and not line.startswith('  (via '))+'\n'

def seg(netid,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {netid}))')

r=[]
# CAN
r += [
    seg(16,94.2,43.0,91.0,43.0,.30), seg(16,91.0,43.0,88.5,41.85,.30),
    seg(16,88.5,41.85,86.0,41.85,.30), seg(17,94.2,45.5,91.2,45.5,.30),
    seg(17,91.2,45.5,88.5,43.15,.30), seg(17,88.5,43.15,86.0,43.15,.30),
    seg(16,86.0,41.85,74.7,41.865,.30), seg(17,86.0,43.15,74.7,43.135,.30),
    seg(20,94.2,48.0,91.0,48.0,.30), seg(20,91.0,48.0,88.5,48.85,.30),
    seg(20,88.5,48.85,86.0,48.85,.30), seg(21,94.2,50.5,91.2,50.5,.30),
    seg(21,91.2,50.5,88.5,50.15,.30), seg(21,88.5,50.15,86.0,50.15,.30),
    seg(20,86.0,48.85,80.0,48.85,.30), seg(20,80.0,48.85,74.7,47.865,.30),
    seg(21,86.0,50.15,80.0,50.15,.30), seg(21,80.0,50.15,74.7,49.135,.30),
]
# GNSS
r += [seg(52,45.15,13.7,42.5,13.7,.40), seg(52,42.5,13.7,37.95,13.0,.40)]
# 24 V and VIN. C1 rot270 + IPC pitch: VIN pad1 is y=55.525.
r += [
    seg(2,97.8,53.0,84.35,53.0,.80),
    seg(3,79.65,53.0,79.65,56.0,.80), seg(3,79.65,56.0,77.5,60.0,.80),
    seg(3,77.5,60.0,75.0,56.4,.80), seg(3,75.0,56.4,70.0,56.4,.80),
    seg(3,70.0,56.4,66.0,55.525,.60), seg(3,66.0,55.525,63.5,55.525,.60),
    seg(3,63.5,55.525,62.0,55.525,.45), seg(3,62.0,55.525,62.0,58.5,.35),
    seg(3,62.0,58.5,60.15,58.5,.30),
]
# SW + bootstrap
r += [
    seg(7,55.85,59.0,54.8,59.0,.25), seg(7,54.8,59.0,53.5,59.0,.55),
    seg(7,53.5,59.0,53.5,56.0,.40), seg(7,53.5,56.0,56.2,54.9,.40),
    seg(7,55.85,59.0,56.0,60.0,.25), seg(7,56.0,60.0,57.5,61.5,.25),
    seg(8,60.15,59.0,60.0,60.1,.25), seg(8,60.0,60.1,58.5,61.5,.25),
]
# 3V3. C7/C8 rot180 + IPC pitch: pad1 faces right at x=44.475.
r += [
    seg(9,47.5,59.0,46.0,59.0,.80),
    seg(9,46.0,59.0,46.0,55.5,.60), seg(9,46.0,55.5,44.475,55.5,.60),
    seg(9,46.0,59.0,46.0,60.5,.60), seg(9,46.0,60.5,44.475,60.5,.60),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v5 IPC-1210 cleanup to {P}')
