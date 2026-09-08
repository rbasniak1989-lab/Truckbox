from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.A v7: replace guessed power-pad geometry with coordinates verified by
# KiCad 10. Keep the filled copper planes from v6, but remove the aggressive
# per-pad 3V3 fanout that created clearance violations. Low-speed routing comes
# in the next pass after this critical power geometry is clean.


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

# Correct physical orientation confirmed from KiCad 10 coordinates:
# C3 rot90 -> VIN pad1 y56.4, GND pad2 y62.6.
# D5 rot270 -> SW pad2 y55.7, GND pad1 y50.9.
s=edit_ref(s,'C3',lambda b:set_at(b,70.0,59.5,90))
s=edit_ref(s,'D5',lambda b:set_at(b,56.2,53.3,270))

# Remove v6 critical power and fanout copper. Preserve CAN/GNSS and all zones.
# Net IDs: 1 GND, 3 VIN_PROT, 7 SW_NODE, 8 BOOT_SW, 9 3V3_MAIN.
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

# Protected VIN. Every branch is routed to the actual VIN pad, never through
# the GND pad of the same capacitor.
# D2.2 VIN=(79.65,53), D1.2=(77.5,60), C2.1=(67.5,51.025),
# C3.1=(70,56.4), C1.1=(63.5,55.525), U6.2=(60.15,58.5).
r += [
    # TVS branch
    seg(3,79.65,53.0,80.0,56.0,.80),
    seg(3,80.0,56.0,77.5,60.0,.80),
    # Input ceramic C2 branch kept above its GND pad
    seg(3,79.65,53.0,75.0,49.8,.65),
    seg(3,75.0,49.8,67.5,49.8,.60),
    seg(3,67.5,49.8,67.5,51.025,.55),
    # Bulk C3 branch
    seg(3,79.65,53.0,76.0,54.5,.70),
    seg(3,76.0,54.5,70.0,56.4,.65),
    # C3 -> C1
    seg(3,70.0,56.4,66.5,55.525,.55),
    seg(3,66.5,55.525,63.5,55.525,.50),
    # C1 -> TPS54260 VIN: narrow escape between 0.5-mm pitch pins
    seg(3,63.5,55.525,62.0,56.2,.40),
    seg(3,62.0,56.2,61.2,57.0,.32),
    seg(3,61.2,57.0,61.2,58.5,.25),
    seg(3,61.2,58.5,60.15,58.5,.22),
]

# Buck SW loop. U6.10=(55.85,59), L1.2=(53.5,59), D5.2=(56.2,55.7).
r += [
    seg(7,55.85,59.0,53.5,59.0,.60),
    # Catch diode branch leaves from inductor side, avoiding U6 pins 6..9.
    seg(7,53.5,59.0,52.6,57.5,.40),
    seg(7,52.6,57.5,52.6,55.7,.35),
    seg(7,52.6,55.7,56.2,55.7,.35),
]

# Bootstrap capacitor C4: pad2=SW at (57.5,61.5), pad1=BOOT at (58.5,61.5).
r += [
    seg(7,55.85,59.0,56.35,60.15,.22),
    seg(7,56.35,60.15,57.5,61.5,.22),
    seg(8,60.15,59.0,60.6,60.1,.22),
    seg(8,60.6,60.1,58.5,61.5,.22),
]

# 3V3 output only: L1 -> C7/C8 plus ONE source via to In2.Cu plane.
r += [
    seg(9,47.5,59.0,46.0,59.0,.80),
    seg(9,46.0,59.0,46.0,55.5,.60),
    seg(9,46.0,55.5,44.475,55.5,.60),
    seg(9,46.0,59.0,46.0,60.5,.60),
    seg(9,46.0,60.5,44.475,60.5,.60),
    seg(9,47.5,59.0,48.5,57.2,.60),
    via(9,48.5,57.2,.90,.45),
]

# Conservative GND stitching. Avoid the 24V/3V3 power pads and MCU antenna edges.
for x,y in [
    (12,28),(24,28),(38,28),(48,30),(62,30),(76,30),(90,30),
    (10,52),(24,52),(36,52),(76,63),(92,58),(36,10),(62,11),(94,36),
    (58,49),(73,56)
]:
    r.append(via(1,x,y,.80,.40))

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v7 verified power geometry to {P}')
