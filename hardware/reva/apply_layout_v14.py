from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')
net_id={name:int(i) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)}

# Rev.A v14
# - relocate CAN1 TX recessive pull-up away from C11
# - consolidate LINK_EN back-layer junction (remove near-coincident vias)
# - route local WDT_3V3 and 3V3_SD power buses on B.Cu


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

# R3 moved above-left of U3/C11. pad1=3V3_MAIN x64.5, pad2=TX_SAFE x65.5.
s=edit_ref(s,'R3',lambda b:set_at(b,65.0,39.0,0))

# Remove prior routes/vias for nets rebuilt here.
ids={net_id[n] for n in ('CAN1_TX_SAFE','LINK_EN','WDT_3V3','3V3_SD')}
new=[]
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m=re.search(r'\(net (\d+)\)',line)
        if m and int(m.group(1)) in ids:
            continue
    new.append(line)
s='\n'.join(new)+'\n'


def seg(net,x1,y1,x2,y2,w=.22,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {net_id[net]}))')

def via(net,x,y,size=.70,drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") (net {net_id[net]}))')

r=[]
# CAN1 TX-safe: simple F.Cu route to relocated R3, no via required.
r += [
    seg('CAN1_TX_SAFE',69.3,40.595,67.8,40.595),
    seg('CAN1_TX_SAFE',67.8,40.595,66.8,39.7),
    seg('CAN1_TX_SAFE',66.8,39.7,65.5,39.0),
]

# LINK_EN: one shared via at (71,16) feeds both R14 and C20 branch.
r += [
    seg('LINK_EN',73.25,10.48,71.0,10.48), via('LINK_EN',71.0,10.48),
    seg('LINK_EN',71.0,10.48,71.0,16.0,.22,'B.Cu'), via('LINK_EN',71.0,16.0),
    seg('LINK_EN',71.0,16.0,69.5,16.0),
    seg('LINK_EN',71.0,16.0,67.0,19.0,.22,'B.Cu'), via('LINK_EN',67.0,19.0),
    seg('LINK_EN',67.0,19.0,68.5,19.0),
]

# WDT_3V3. Three endpoints tied by a short back-layer tree.
r += [
    seg('WDT_3V3',20.9,36.95,19.2,36.95,.30), via('WDT_3V3',19.2,36.95),
    seg('WDT_3V3',17.5,36.0,16.0,36.0,.30), via('WDT_3V3',16.0,36.0),
    seg('WDT_3V3',23.05,31.5,24.8,31.5,.30), via('WDT_3V3',24.8,31.5),
    seg('WDT_3V3',16.0,36.0,19.2,36.95,.40,'B.Cu'),
    seg('WDT_3V3',19.2,36.95,24.8,31.5,.40,'B.Cu'),
]

# 3V3_SD. B.Cu trunk from Q2 to microSD, C18 and R25.
r += [
    seg('3V3_SD',56.55,33.0,58.3,33.0,.35), via('3V3_SD',58.3,33.0),
    # J1 pin4 escapes upward from the connector row.
    seg('3V3_SD',46.195,36.55,46.195,34.5,.30), via('3V3_SD',46.195,34.5),
    # C18 pad1 exits left.
    seg('3V3_SD',55.4,39.0,53.6,39.0,.30), via('3V3_SD',53.6,39.0),
    # R25 pad1 exits left, away from SD_CS pad2.
    seg('3V3_SD',55.5,42.0,53.8,42.0,.30), via('3V3_SD',53.8,42.0),
    # back-layer tree
    seg('3V3_SD',58.3,33.0,52.0,33.0,.50,'B.Cu'),
    seg('3V3_SD',52.0,33.0,46.195,34.5,.50,'B.Cu'),
    seg('3V3_SD',52.0,33.0,53.6,39.0,.50,'B.Cu'),
    seg('3V3_SD',53.6,39.0,53.8,42.0,.50,'B.Cu'),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v14 SD/WDT power and cleanup to {P}')
