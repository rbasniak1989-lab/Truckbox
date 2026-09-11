from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Run-170 follow-up.  This pass only replaces the local ACC fanout.  It keeps
# the validated right-angle J4 and CAN routes untouched, and routes around the
# fixed USB/UART copper that the previous compact placement approached too
# closely.


def balanced_block(text, start):
    depth=0; in_q=False; esc=False
    for j in range(start,len(text)):
        c=text[j]
        if in_q:
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=='"': in_q=False
            continue
        if c=='"': in_q=True
        elif c=='(': depth+=1
        elif c==')':
            depth-=1
            if depth==0: return j+1
    raise RuntimeError('unterminated s-expression')


def iter_blocks(text,token):
    i=0; needle='('+token
    while True:
        i=text.find(needle,i)
        if i<0: return
        j=balanced_block(text,i)
        yield i,j,text[i:j]
        i=j


def ref_in_footprint(block,ref):
    return (
        re.search(r'\(property\s+"Reference"\s+"'+re.escape(ref)+r'"',block) is not None
        or re.search(r'\(fp_text\s+reference\s+"?'+re.escape(ref)+r'"?(?:\s|\))',block) is not None
    )


def find_footprint(text,ref):
    for i,j,b in iter_blocks(text,'footprint'):
        if ref_in_footprint(b,ref): return i,j,b
    raise RuntimeError(f'footprint {ref} not found')


def move_footprint(text,ref,x,y,rot=0):
    i,j,b=find_footprint(text,ref)
    nb,n=re.subn(r'\(at\s+[-+0-9.]+\s+[-+0-9.]+(?:\s+[-+0-9.]+)?\)',
                 f'(at {x:.3f} {y:.3f} {rot})',b,count=1)
    if n!=1: raise RuntimeError(f'could not move {ref}')
    return text[:i]+nb+text[j:]


net_id={name:int(idx) for idx,name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)',s)}
for name in ('ACC_RAW','ACC_MID1','ACC_BASE','ACC_N'):
    if name not in net_id: raise RuntimeError(f'net {name} missing')
inv={v:k for k,v in net_id.items()}


def net_expr(name): return f'(net {net_id[name]})'


def seg_info(b):
    ms=re.search(r'\(start\s+([-+0-9.]+)\s+([-+0-9.]+)\)',b)
    me=re.search(r'\(end\s+([-+0-9.]+)\s+([-+0-9.]+)\)',b)
    ml=re.search(r'\(layer\s+"([^"]+)"\)',b)
    if not(ms and me and ml): return None
    mn=re.search(r'\(net\s+"([^"]+)"\)',b)
    if mn: name=mn.group(1)
    else:
        mi=re.search(r'\(net\s+(\d+)\)',b)
        if not mi: return None
        name=inv.get(int(mi.group(1)))
    return name,tuple(map(float,ms.groups())),tuple(map(float,me.groups())),ml.group(1)


# Q3 returns to the Run-168 vertical coordinate (which cleared D6/USB_D- once
# the obsolete GND stitch is absent). D7 is moved into the open strip above the
# CAN bundle, where its GND pad can receive a normal thermal relief.
s=move_footprint(s,'Q3',87.0,34.5,0)
s=move_footprint(s,'R28',89.9,35.5,0)
s=move_footprint(s,'R10',92.2,35.5,0)
s=move_footprint(s,'D7',79.0,36.0,0)

# Delete the previous local ACC routes. Preserve the long ACC_N trunk ending at
# (83.5,40); only its local right-side tail is replaced.
rm=[]
for a,b,blk in iter_blocks(s,'segment'):
    info=seg_info(blk)
    if not info: continue
    name,p1,p2,layer=info
    minx=min(p1[0],p2[0])
    if name in {'ACC_RAW','ACC_MID1','ACC_BASE'}:
        rm.append((a,b))
    elif name=='ACC_N' and minx>=82.70:
        rm.append((a,b))
for a,b in reversed(rm): s=s[:a]+s[b:]

# Clear all local ACC vias from the prior passes. Keep only the two far-upstream
# ACC_N vias (x=37.5 and 49.0). Also scrub the four historical stale vias by
# geometry regardless of how an older KiCad serialization labels their net.
stale=[(86.0,36.0),(94.0,36.0),(91.29,49.25),(91.68,44.25)]
def near(xy,t,tol=.13): return abs(xy[0]-t[0])<=tol and abs(xy[1]-t[1])<=tol
vrm=[]
for a,b,blk in iter_blocks(s,'via'):
    ma=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)\)',blk)
    if not ma: continue
    xy=tuple(map(float,ma.groups()))
    mn=re.search(r'\(net\s+"([^"]+)"\)',blk)
    if mn: name=mn.group(1)
    else:
        mi=re.search(r'\(net\s+(\d+)\)',blk)
        name=inv.get(int(mi.group(1))) if mi else None
    drop=(name in {'ACC_RAW','ACC_MID1','ACC_BASE'})
    if name=='ACC_N' and xy[0]>60: drop=True
    if any(near(xy,t) for t in stale): drop=True
    if drop: vrm.append((a,b))
for a,b in reversed(vrm): s=s[:a]+s[b:]


def seg(name,x1,y1,x2,y2,w=.22,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") {net_expr(name)})')

def via(name,x,y,size=.70,drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") {net_expr(name)})')

# Pad centers used below:
# Q3 @87,34.5: p1 ACC_BASE=85.95,35.45; p3 ACC_N=88.05,34.50
# R28 @89.9,35.5: p1 ACC_MID1=89.40; p2 ACC_BASE=90.40
# R10 @92.2,35.5: p1 ACC_RAW=91.70; p2 ACC_MID1=92.70
# D7 @79,36: p2 ACC_BASE=80.05,36.00
items=[
    # ACC_MID1: escape from the two OUTER resistor pads, then join on B.Cu.
    # This avoids crossing R28 p2 / R10 p1 on the top layer.
    seg('ACC_MID1',89.40,35.50,88.60,35.50,.22,'F.Cu'),
    seg('ACC_MID1',88.60,35.50,88.60,36.20,.22,'F.Cu'),
    via('ACC_MID1',88.60,36.20),
    seg('ACC_MID1',92.70,35.50,93.50,35.50,.22,'F.Cu'),
    seg('ACC_MID1',93.50,35.50,93.50,36.20,.22,'F.Cu'),
    via('ACC_MID1',93.50,36.20),
    seg('ACC_MID1',88.60,36.20,93.50,36.20,.22,'B.Cu'),

    # ACC_RAW: p1 of R10 drops below the fixed USB_CC1 endpoint before the via.
    # The x=95.5 back-layer spine remains comfortably left of J4's MP hole.
    seg('ACC_RAW',91.70,35.50,91.70,38.00,.24,'F.Cu'),
    via('ACC_RAW',91.70,38.00),
    seg('ACC_RAW',91.70,38.00,95.50,38.00,.28,'B.Cu'),
    seg('ACC_RAW',87.60,43.50,88.60,44.20,.24,'F.Cu'),
    seg('ACC_RAW',88.60,44.20,90.00,45.50,.24,'F.Cu'),
    via('ACC_RAW',90.00,45.50),
    seg('ACC_RAW',90.00,45.50,95.50,45.50,.28,'B.Cu'),
    seg('ACC_RAW',95.50,45.50,95.50,38.00,.28,'B.Cu'),

    # ACC_BASE Q3/R28 branch: no via is placed near USB_CC1. The central stitch
    # at 86.8,38.5 is well below the fixed service traces.
    seg('ACC_BASE',85.95,35.45,86.80,38.50,.22,'F.Cu'),
    seg('ACC_BASE',90.40,35.50,90.40,38.50,.22,'F.Cu'),
    seg('ACC_BASE',90.40,38.50,86.80,38.50,.22,'F.Cu'),
    via('ACC_BASE',86.80,38.50),

    # ACC_BASE D7/R11 branch approaches R11 pad1 from the left/below, leaving
    # its adjacent GND pad free for two thermal spokes, then joins on B.Cu.
    seg('ACC_BASE',80.05,36.00,80.05,38.00,.22,'F.Cu'),
    seg('ACC_BASE',80.05,38.00,83.50,38.00,.22,'F.Cu'),
    seg('ACC_BASE',83.50,38.00,83.50,37.00,.22,'F.Cu'),
    via('ACC_BASE',83.50,38.00),
    seg('ACC_BASE',83.50,38.00,86.80,38.50,.24,'B.Cu'),

    # ACC_N trunk -> R12: go left of the ACC_BASE cluster, change layers only
    # below USB_CC2, return to F.Cu at x=82, and approach R12 pad2 from its
    # right side through a common node at (84.8,33.0).
    seg('ACC_N',83.50,40.00,82.00,40.00,.22,'F.Cu'),
    via('ACC_N',82.00,40.00),
    seg('ACC_N',82.00,40.00,82.00,36.20,.22,'B.Cu'),
    via('ACC_N',82.00,36.20),
    seg('ACC_N',82.00,36.20,82.00,33.00,.22,'F.Cu'),
    seg('ACC_N',82.00,33.00,84.80,33.00,.22,'F.Cu'),
    seg('ACC_N',84.80,33.00,84.80,34.00,.22,'F.Cu'),
    seg('ACC_N',84.80,34.00,84.50,34.00,.22,'F.Cu'),

    # Q3 p3 joins the same ACC_N node on B.Cu by passing to the RIGHT of the
    # USB_CC2 endpoint, then above it. This avoids Q3's GND pad and D6.
    seg('ACC_N',88.05,34.50,87.40,35.80,.22,'F.Cu'),
    via('ACC_N',87.40,35.80),
    seg('ACC_N',87.40,35.80,86.80,33.00,.22,'B.Cu'),
    seg('ACC_N',86.80,33.00,84.80,33.00,.22,'B.Cu'),
    via('ACC_N',84.80,33.00),
]

close=s.rfind(')')
if close<0: raise RuntimeError('board closing paren not found')
s=s[:close]+'\n'+'\n'.join(items)+'\n'+s[close:]

expected={
    'Q3':'(at 87.000 34.500 0)',
    'R28':'(at 89.900 35.500 0)',
    'R10':'(at 92.200 35.500 0)',
    'D7':'(at 79.000 36.000 0)',
    'J4':'(at 97.700 45.000 270)',
}
for ref,at in expected.items():
    _,_,blk=find_footprint(s,ref)
    if at not in blk: raise RuntimeError(f'{ref} placement postcondition failed')

P.write_text(s,encoding='utf-8')
print(f'Applied Run-170 local ACC reroute to {P}')
