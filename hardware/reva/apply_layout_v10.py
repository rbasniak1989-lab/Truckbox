from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.A v10 - clean buck switch geometry after the v9 DRC.
# D5 is vertical, above U6 and right of the feedback divider.  The PH escape
# stays narrow until it has cleared adjacent HVSSOP pins, then widens.


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

net_id={name:int(i) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)}

# D5 rot270: GND pad1 is above, SW pad2 is below.  At y=52 this places
# SW at about y=54.4 and clears both U6 and R6/R7 courtyards.
s=edit_ref(s,'D5',lambda b:set_at(b,56.2,52.0,270))

# Remove every old SW_NODE segment and any obsolete via at 38,28.
new=[]
for line in s.splitlines():
    if line.startswith('  (segment '):
        m=re.search(r'\(net (\d+)\)',line)
        if m and int(m.group(1))==net_id['SW_NODE']:
            continue
    if line.startswith('  (via '):
        at=re.search(r'\(at ([0-9.]+) ([0-9.]+)\)',line)
        if at and abs(float(at.group(1))-38.0)<1e-6 and abs(float(at.group(2))-28.0)<1e-6:
            continue
    new.append(line)
s='\n'.join(new)+'\n'


def seg(netname,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {net_id[netname]}))')

r=[
    # PH pad10 -> L1.2: 0.18 mm escape first, then widen.
    seg('SW_NODE',55.85,59.0,54.75,59.0,.18),
    seg('SW_NODE',54.75,59.0,53.5,59.0,.50),
    # Catch diode branch leaves left/down of U6, then approaches D5 from below.
    seg('SW_NODE',53.5,59.0,52.8,57.4,.35),
    seg('SW_NODE',52.8,57.4,53.5,56.0,.30),
    seg('SW_NODE',53.5,56.0,56.2,54.4,.30),
    # Bootstrap SW side; also narrow at PH.
    seg('SW_NODE',55.85,59.0,56.35,60.15,.18),
    seg('SW_NODE',56.35,60.15,57.5,61.5,.20),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v10 buck switch cleanup to {P}')
