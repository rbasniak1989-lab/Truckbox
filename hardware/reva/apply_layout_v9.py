from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.A v9
# 1) Split the two UART series resistors into true nets on each side.
#    Previous revisions used the same net name on both resistor pads, which
#    would make KiCad request copper that bypasses the resistor.
# 2) Move D5 away from L1 and rebuild only the SW_NODE copper.
# 3) Remove the obsolete/dangling CORE_TO_LINK via from the old prototype pass.


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

# Existing net IDs are intentionally preserved. Append new nets at the end.
net_pairs=[(int(i),name) for i,name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$',s,re.M)]
net_id={name:i for i,name in net_pairs}
next_id=max(i for i,_ in net_pairs)+1
for name in ('CORE_TO_LINK_MCU','LINK_TO_CORE_MCU'):
    if name not in net_id:
        net_id[name]=next_id; next_id+=1
        m=re.search(r'\n  \(gr_line ',s)
        if not m: raise RuntimeError('cannot locate end of top-level net table')
        s=s[:m.start()]+f'\n  (net {net_id[name]} "{name}")'+s[m.start():]


def set_pad_net(blk,padno,netname):
    nid=net_id[netname]
    lines=blk.splitlines()
    hit=0
    for i,line in enumerate(lines):
        if f'(pad "{padno}" ' in line:
            if '(net ' in line:
                line=re.sub(r'\(net \d+ "[^"]+"\)',f'(net {nid} "{netname}")',line)
            else:
                line=line[:-1]+f' (net {nid} "{netname}"))'
            lines[i]=line; hit+=1
    if hit!=1: raise RuntimeError(f'{padno=} not uniquely found')
    return '\n'.join(lines)

# Core TX -> R33 -> Link RX.
s=edit_ref(s,'U1',lambda b:set_pad_net(b,'20','CORE_TO_LINK_MCU'))
s=edit_ref(s,'R33',lambda b:set_pad_net(b,'1','CORE_TO_LINK_MCU'))
# Link TX -> R34 -> Core RX.
s=edit_ref(s,'U2',lambda b:set_pad_net(b,'20','LINK_TO_CORE_MCU'))
s=edit_ref(s,'R34',lambda b:set_pad_net(b,'1','LINK_TO_CORE_MCU'))

# D5 horizontal and above L1: pad1/GND=(48.6,53), pad2/SW=(53.4,53).
s=edit_ref(s,'D5',lambda b:set_at(b,51.0,53.0,0))

# Remove previous SW_NODE tracks and the old dangling UART via.
new=[]
core_to_link_id=net_id['CORE_TO_LINK']
for line in s.splitlines():
    if line.startswith('  (segment '):
        m=re.search(r'\(net (\d+)\)',line)
        if m and int(m.group(1))==net_id['SW_NODE']:
            continue
    if line.startswith('  (via '):
        m=re.search(r'\(net (\d+)\)',line)
        at=re.search(r'\(at ([0-9.]+) ([0-9.]+)\)',line)
        if m and at and int(m.group(1))==core_to_link_id and abs(float(at.group(1))-38.0)<1e-6 and abs(float(at.group(2))-28.0)<1e-6:
            continue
    new.append(line)
s='\n'.join(new)+'\n'


def seg(netname,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {net_id[netname]}))')

r=[]
# PH pin -> L1.2. Branch to D5.2 outside L1 courtyard.
r += [
    seg('SW_NODE',55.85,59.0,53.5,59.0,.55),
    seg('SW_NODE',53.5,59.0,54.6,57.0,.40),
    seg('SW_NODE',54.6,57.0,54.6,54.2,.35),
    seg('SW_NODE',54.6,54.2,53.4,53.0,.35),
    # C4 bootstrap SW side remains local to PH.
    seg('SW_NODE',55.85,59.0,56.4,60.2,.20),
    seg('SW_NODE',56.4,60.2,57.5,61.5,.20),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v9 UART-net and D5 fixes to {P}')
