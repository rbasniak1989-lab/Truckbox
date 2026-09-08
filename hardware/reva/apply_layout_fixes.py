from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# --- helpers ---------------------------------------------------------------
def blocks(text, token='  (footprint '):
    out=[]; i=0
    while True:
        start=text.find(token,i)
        if start<0: break
        depth=0; in_str=False; esc=False; end=None
        for j in range(start,len(text)):
            c=text[j]
            if in_str:
                if esc: esc=False
                elif c=='\\': esc=True
                elif c=='"': in_str=False
            else:
                if c=='"': in_str=True
                elif c=='(': depth+=1
                elif c==')':
                    depth-=1
                    if depth==0:
                        end=j+1; break
        if end is None: raise RuntimeError('unbalanced footprint')
        out.append((start,end,text[start:end])); i=end
    return out

def edit_ref(text, ref, fn):
    for a,b,blk in blocks(text):
        if f'(fp_text reference "{ref}"' in blk:
            return text[:a]+fn(blk)+text[b:]
    raise RuntimeError(f'footprint {ref} not found')

def set_at(blk,x,y,rot=0):
    return re.sub(r'\n    \(at [^\n]+\)', f'\n    (at {x:.3f} {y:.3f} {rot})', blk, count=1)

def set_pad_local(blk,padno,x,y):
    pat=rf'(\(pad "{re.escape(str(padno))}" [^\n]*?\(at )[-0-9.]+ [-0-9.]+'
    repl=rf'\g<1>{x:.3f} {y:.3f}'
    out,n=re.subn(pat,repl,blk,count=1)
    if n!=1: raise RuntimeError(f'pad {padno} not found')
    return out

def swap_pad_nets(blk,p1,p2,net1_id,net1_name,net2_id,net2_name):
    # Swap only the net clauses on the selected pad lines.
    lines=blk.splitlines()
    for idx,line in enumerate(lines):
        if f'(pad "{p1}" ' in line:
            lines[idx]=re.sub(r'\(net \d+ "[^"]+"\)', f'(net {net2_id} "{net2_name}")', line)
        elif f'(pad "{p2}" ' in line:
            lines[idx]=re.sub(r'\(net \d+ "[^"]+"\)', f'(net {net1_id} "{net1_name}")', line)
    return '\n'.join(lines)

# J4: keep external pin numbers unchanged, but optimize PCB pad geometry.
# row 1 = CAN1; row 2 = ACC/reserve; row 3 = CAN2; row 4 = B+/GND.
def fix_j4(blk):
    blk=set_at(blk,96,48,0)
    pos={4:(-1.8,-4.5),5:(1.8,-4.5),3:(-1.8,-1.5),8:(1.8,-1.5),
         6:(-1.8,1.5),7:(1.8,1.5),1:(-1.8,4.5),2:(1.8,4.5)}
    for p,(x,y) in pos.items(): blk=set_pad_local(blk,p,x,y)
    return blk
s=edit_ref(s,'J4',fix_j4)

# USB-C: connector mouth on right edge, contact row inside PCB.
s=edit_ref(s,'J3',lambda b:set_at(b,97,27,270))

# CAN TVS is symmetrical. Put H on pad2 and L on pad1 so the post-TVS pair
# runs directly into TCAN CANH/CANL without crossing.
s=edit_ref(s,'D3',lambda b:swap_pad_nets(b,1,2,16,'CAN1_H',17,'CAN1_L'))
s=edit_ref(s,'D4',lambda b:swap_pad_nets(b,1,2,20,'CAN2_H',21,'CAN2_L'))

# Remove first-pass routed segments; replace only critical, coordinate-checked routes.
s='\n'.join(line for line in s.splitlines() if not line.startswith('  (segment '))+'\n'

def seg(netid,x1,y1,x2,y2,w=0.35,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") (net {netid}))'

routes=[]
# CAN1: J4 p4/p5 -> D3 -> U3. D3 p2=H, p1=L after swap.
routes += [
 seg(16,94.2,43.5,90.0,42.4), seg(16,90.0,42.4,86.0,42.35),
 seg(17,97.8,43.5,91.0,43.8), seg(17,91.0,43.8,86.0,43.65),
 seg(16,86.0,42.35,80.0,42.35), seg(16,80.0,42.35,74.7,42.365),
 seg(17,86.0,43.65,80.0,43.65), seg(17,80.0,43.65,74.7,43.635),
]
# CAN2: J4 p6/p7 -> D4 -> U4. D4 p2=H, p1=L after swap.
routes += [
 seg(20,94.2,49.5,90.0,51.2), seg(20,90.0,51.2,86.0,51.35),
 seg(21,97.8,49.5,91.0,52.5), seg(21,91.0,52.5,86.0,52.65),
 seg(20,86.0,51.35,80.0,51.35), seg(20,80.0,51.35,74.7,51.365),
 seg(21,86.0,52.65,80.0,52.65), seg(21,80.0,52.65,74.7,52.635),
]
# GNSS RF: U5 RF_IN pad11 -> U.FL pad1. Wide, short top-layer route.
routes += [seg(52,45.15,12.7,47.0,9.0,0.70), seg(52,47.0,9.0,48.95,4.0,0.70)]

# Insert immediately before final PCB close paren.
pos=s.rfind('\n)')
if pos<0: raise RuntimeError('board final paren not found')
s=s[:pos]+'\n'+'\n'.join(routes)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A layout fixes to {P}')
