from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Rev.A v3: remove the remaining geometry conflicts in the critical blocks
# before routing the low-speed nets.  This patch intentionally runs after
# apply_layout_fixes.py.

def blocks(text, token='  (footprint '):
    out=[]; i=0
    while True:
        a=text.find(token,i)
        if a < 0: break
        depth=0; ins=False; esc=False; b=None
        for j in range(a,len(text)):
            c=text[j]
            if ins:
                if esc: esc=False
                elif c=='\\': esc=True
                elif c=='"': ins=False
            else:
                if c=='"': ins=True
                elif c=='(': depth += 1
                elif c==')':
                    depth -= 1
                    if depth == 0:
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

def delete_ref(text,ref):
    for a,b,blk in blocks(text):
        if ref_of(blk)==ref:
            return text[:a]+text[b:]
    return text

def set_at(blk,x,y,rot=0):
    return re.sub(r'\n    \(at [^\n]+\)',f'\n    (at {x:.3f} {y:.3f} {rot})',blk,count=1)

def set_pad_local(blk,padno,x,y):
    pat=rf'(\(pad "{re.escape(str(padno))}" [^\n]*?\(at )[-0-9.]+ [-0-9.]+'
    out,n=re.subn(pat,rf'\g<1>{x:.3f} {y:.3f}',blk,count=1)
    if n != 1: raise RuntimeError(f'pad {padno} not found in footprint')
    return out

# ---- Physical repack -------------------------------------------------------
# Keep the MCU/RF/storage placement from v2.  Repack only the noisy power
# block and service/test area.
moves={
    'U6':(58.0,58.0,180),
    'L1':(50.5,59.0,0),
    'D5':(56.2,52.5,90),
    'C4':(60.5,53.5,90),
    'C1':(64.0,57.0,90),
    'C2':(67.5,52.5,90),
    'D1':(85.0,59.5,180),
    'D2':(82.0,50.5,180),
    'R31':(62.0,47.0,0),
    'R32':(65.0,47.0,0),
    'R8':(58.0,63.0,0),
    'C5':(61.0,63.0,0),
    'R9':(61.5,49.5,0),
    'C6':(64.0,49.5,0),
    'C7':(43.0,55.5,0),
    'C8':(43.0,60.5,0),
    'C13':(35.0,36.0,0),
    'TP1':(8.0,61.0,0),
    'TP2':(11.0,61.0,0),
    'TP3':(14.0,61.0,0),
    'TP4':(17.0,61.0,0),
    'TP5':(20.0,61.0,0),
    'TP6':(23.0,61.0,0),
}
for ref,(x,y,r) in moves.items():
    s=edit_ref(s,ref,lambda b,x=x,y=y,r=r:set_at(b,x,y,r))

# The bulk capacitor is round mechanically.  Rotate its pads so VIN is on the
# upper side and GND on the lower side, keeping both out of the 24V rail path.
s=edit_ref(s,'C3',lambda b:set_at(b,72.0,59.5,270))

# TP17 duplicated PWRGD_TP from the earlier prototype floorplan.
s=delete_ref(s,'TP17')

# J4 is a pigtail pad field, not a keyed connector.  Electrical pin numbers
# remain unchanged; only the physical pad order is optimized so CAN pairs leave
# the board without crossing and B+/GND sit below them.
def fix_j4(blk):
    blk=set_at(blk,96.0,48.0,0)
    pos={
        4:(-1.8,-5.0),  # CAN1_H
        5:(-1.8,-2.5),  # CAN1_L
        6:(-1.8, 0.0),  # CAN2_H
        7:(-1.8, 2.5),  # CAN2_L
        3:( 1.8,-2.5),  # ACC
        8:( 1.8, 0.0),  # reserve
        2:( 1.8, 2.5),  # GND
        1:( 1.8, 5.0),  # B+
    }
    for p,(x,y) in pos.items():
        blk=set_pad_local(blk,p,x,y)
    return blk
s=edit_ref(s,'J4',fix_j4)

# ---- Critical routing, rebuilt from scratch -------------------------------
# v2 only routed critical nets, therefore it is safe to replace every segment
# and via here.  Low-speed nets will be routed after this stage is DRC-clean.
s='\n'.join(line for line in s.splitlines()
            if not line.startswith('  (segment ') and not line.startswith('  (via '))+'\n'

def seg(netid,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {netid}))')

r=[]

# CAN1: J4 pins are now vertically separated, so neither line crosses a pad.
r += [
    seg(16,94.2,43.0,91.0,43.0,.30),
    seg(16,91.0,43.0,88.5,41.85,.30),
    seg(16,88.5,41.85,86.0,41.85,.30),
    seg(17,94.2,45.5,91.2,45.5,.30),
    seg(17,91.2,45.5,88.5,43.15,.30),
    seg(17,88.5,43.15,86.0,43.15,.30),
    seg(16,86.0,41.85,74.7,41.865,.30),
    seg(17,86.0,43.15,74.7,43.135,.30),
]

# CAN2.
r += [
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

# GNSS RF path kept short and isolated from the buck section.
r += [
    seg(52,45.15,13.7,42.5,13.7,.40),
    seg(52,42.5,13.7,37.95,13.0,.40),
]

# 24V input.  J4 B+ is the lowest right-column pad; the trace stays below the
# CAN pads. D2 is above D1 so the protected VIN can enter D1 on its left pad.
r += [
    seg(2,97.8,53.0,90.0,53.0,.80),
    seg(2,90.0,53.0,84.35,50.5,.80),
    seg(3,79.65,50.5,79.65,56.5,.80),
    seg(3,79.65,56.5,79.0,59.5,.80),
    seg(3,79.0,59.5,76.0,56.4,.80),
    seg(3,76.0,56.4,72.0,56.4,.80),
    # C3 VIN -> C1 VIN -> U6 VIN.  Narrow only at the HVSSOP escape.
    seg(3,72.0,56.4,68.0,55.9,.60),
    seg(3,68.0,55.9,64.0,55.9,.60),
    seg(3,64.0,55.9,62.5,55.9,.45),
    seg(3,62.5,55.9,62.5,58.5,.35),
    seg(3,62.5,58.5,60.15,58.5,.30),
]

# Buck switch node.  The first escape from U6 is intentionally 0.25 mm so it
# clears the adjacent 0.5-mm-pitch pins before widening at the inductor.
r += [
    seg(7,55.85,59.0,54.8,59.0,.25),
    seg(7,54.8,59.0,53.5,59.0,.55),
    seg(7,53.5,59.0,53.5,56.0,.40),
    seg(7,53.5,56.0,56.2,54.9,.40),
    seg(7,56.2,54.9,58.5,54.9,.35),
    seg(7,58.5,54.9,60.5,54.0,.30),
]

# Bootstrap: leave U6 pin 1 to the right before turning upward so it does not
# graze VIN/EN pins.
r += [
    seg(8,60.15,59.0,61.5,59.0,.25),
    seg(8,61.5,59.0,61.5,53.0,.25),
    seg(8,61.5,53.0,60.5,53.0,.25),
]

# 3V3 output rail to both output capacitors.  Test pads were moved away.
r += [
    seg(9,47.5,59.0,45.5,59.0,.80),
    seg(9,45.5,59.0,45.5,55.5,.60),
    seg(9,45.5,55.5,41.9,55.5,.60),
    seg(9,45.5,59.0,45.5,60.5,.60),
    seg(9,45.5,60.5,41.9,60.5,.60),
]

pos=s.rfind('\n)')
if pos < 0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v3 critical-block cleanup to {P}')
