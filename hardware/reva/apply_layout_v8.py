from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.A v8 - verified TPS54260 power geometry.
# This pass follows TI layout intent: small VIN loop, narrow pin escapes,
# PH -> inductor/catch diode kept local, and thermal vias under PowerPAD.


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

# C2 rot90: VIN pad1=(67.5,53.975), GND pad2=(67.5,51.025).
# D5 moved left of the IC: GND pad1=(48.6,56), SW pad2=(53.4,56).
s=edit_ref(s,'C2',lambda b:set_at(b,67.5,52.5,90))
s=edit_ref(s,'D5',lambda b:set_at(b,51.0,56.0,0))

# Rebuild critical power copper. Preserve all other signal routes and zones.
remove_nets={1,3,7,8,9}
new=[]
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m=re.search(r'\(net (\d+)\)',line)
        if m and int(m.group(1)) in remove_nets:
            continue
    new.append(line)
s='\n'.join(new)+'\n'


def seg(netid,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {netid}))')


def via(netid,x,y,size=.80,drill=.40):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") (net {netid}))')

r=[]

# VIN_PROT actual pads after v8:
# D2.2=(79.65,53), D1.2=(77.5,60), C2.1=(67.5,53.975),
# C3.1=(70,56.4), C1.1=(63.5,55.525), U6.2=(60.15,58.5).
r += [
    # D2 -> TVS D1 branch, kept to the far-right/bottom side of CAN2.
    seg(3,79.65,53.0,81.0,56.0,.75),
    seg(3,81.0,56.0,80.0,59.0,.75),
    seg(3,80.0,59.0,77.5,60.0,.75),
    # D2 -> C2: stay below CAN2, then approach the VIN pad from the right.
    seg(3,79.65,53.0,78.0,55.0,.60),
    seg(3,78.0,55.0,71.0,55.0,.55),
    seg(3,71.0,55.0,69.5,53.975,.45),
    seg(3,69.5,53.975,67.5,53.975,.40),
    # C2 -> bulk C3.
    seg(3,67.5,53.975,68.5,55.0,.40),
    seg(3,68.5,55.0,70.0,56.4,.45),
    # C3 -> close ceramic C1.
    seg(3,70.0,56.4,66.5,56.4,.50),
    seg(3,66.5,56.4,63.5,55.525,.45),
    # C1 -> VIN pin with a 0.20-mm final escape between adjacent HVSSOP pins.
    seg(3,63.5,55.525,62.2,56.2,.35),
    seg(3,62.2,56.2,61.4,57.2,.28),
    seg(3,61.4,57.2,61.4,58.5,.22),
    seg(3,61.4,58.5,60.15,58.5,.20),
]

# SW_NODE: narrow escape from PH, then widen into L1. Catch diode branches
# from the inductor side so the wide trace does not run along COMP/VSENSE pins.
r += [
    seg(7,55.85,59.0,54.8,59.0,.22),
    seg(7,54.8,59.0,53.5,59.0,.55),
    seg(7,53.5,59.0,53.4,56.0,.45),
]

# Bootstrap capacitor: short, thin traces immediately below the IC.
r += [
    seg(7,55.85,59.0,56.4,60.2,.20),
    seg(7,56.4,60.2,57.5,61.5,.20),
    seg(8,60.15,59.0,60.6,60.2,.20),
    seg(8,60.6,60.2,58.5,61.5,.20),
]

# 3V3 output: L1 -> output caps, then one robust via into the In2 3V3 plane.
r += [
    seg(9,47.5,59.0,46.0,59.0,.80),
    seg(9,46.0,59.0,46.0,55.5,.60),
    seg(9,46.0,55.5,44.475,55.5,.60),
    seg(9,46.0,59.0,46.0,60.5,.60),
    seg(9,46.0,60.5,44.475,60.5,.60),
    seg(9,47.5,59.0,47.5,62.5,.60),
    via(9,47.5,62.5,.90,.45),
]

# PowerPAD thermal vias: directly under U6 exposed GND pad, as recommended by TI.
for x,y in [(57.6,57.6),(58.4,57.6),(57.6,58.4),(58.4,58.4)]:
    r.append(via(1,x,y,.55,.30))

# General GND stitching, deliberately outside the VIN/D1/CAN2 corridor.
for x,y in [
    (12,28),(24,28),(38,28),(48,30),(62,30),(76,30),(90,30),
    (10,52),(24,52),(36,52),(72,63),(92,58),(36,10),(62,11),(94,36),
    (58,49),(84,63)
]:
    r.append(via(1,x,y,.80,.40))

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v8 TPS54260 geometry to {P}')
