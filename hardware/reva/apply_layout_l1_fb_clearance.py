from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# The L1 SW pad at x=53.5 physically blocked the TPS54260 VSENSE escape.
# Shift L1 only 1.0 mm left. Its 2.2-mm-wide pads still overlap the existing
# SW_NODE and 3V3_MAIN copper endpoints, so electrical connectivity is kept
# while creating >0.2 mm manufacturable clearance around the FB via.

def blocks(text, token='\t(footprint '):
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
        if b is None:
            raise RuntimeError('unbalanced footprint')
        out.append((a,b,text[a:b])); i=b
    return out

def ref_of(blk):
    m=re.search(r'\(property "Reference" "([^"]+)"',blk)
    if not m:
        m=re.search(r'\(fp_text reference "?([^"\s\)]+)',blk)
    return m.group(1) if m else None

def set_at(blk,x,y,rot=0):
    return re.sub(r'\n\t\t\(at [^\n]+\)',f'\n\t\t(at {x:.3f} {y:.3f} {rot})',blk,count=1)

for a,b,blk in blocks(s):
    if ref_of(blk)=='L1':
        s=s[:a]+set_at(blk,49.5,59.0,0)+s[b:]
        break
else:
    raise RuntimeError('L1 not found')

P.write_text(s,encoding='utf-8')
print(f'Shifted L1 left 1.0 mm for FB clearance in {P}')
