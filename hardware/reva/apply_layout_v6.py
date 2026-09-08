from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.A v6
# - correct the last power-block geometry using coordinates observed by KiCad DRC
# - establish real copper distribution: GND on F/B + inner GND, 3V3_MAIN plane on In2
# - fan 3V3_MAIN pads to the inner plane with conservative through-vias
# - preserve CAN read-only and short GNSS RF path


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

# C3: VIN pad must face upward toward the protected rail; D5 SW pad faces U6.
s=edit_ref(s,'C3',lambda b:set_at(b,70.0,59.5,270))
s=edit_ref(s,'D5',lambda b:set_at(b,56.2,53.3,90))

# Remove all routed copper from previous patch; rebuild deterministic critical copper below.
s='\n'.join(line for line in s.splitlines()
            if not line.startswith('  (segment ') and not line.startswith('  (via '))+'\n'


def seg(netid,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {netid}))')


def via(netid,x,y,size=.70,drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") (net {netid}))')

r=[]

# CAN1/CAN2: unchanged electrically; no 120R termination; transceiver TXD remains hardware-recessive.
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

# GNSS RF: U5 RF_IN -> U.FL plus active-antenna bias tee.
r += [
    seg(52,45.15,13.7,42.5,13.7,.40),
    seg(52,42.5,13.7,37.95,13.0,.40),
    seg(52,41.5,17.0,42.5,15.8,.25),
    seg(52,42.5,15.8,42.5,13.7,.25),
    # VCC_RF -> L2 pin1, routed around L2 pin2 to avoid crossing the RF node.
    seg(53,45.15,17.0,43.2,17.0,.25),
    seg(53,43.2,17.0,43.2,18.5,.25),
    seg(53,43.2,18.5,40.5,18.5,.25),
    seg(53,40.5,18.5,40.5,17.0,.25),
]

# 24 V B+ to reverse-polarity diode.
r += [seg(2,97.8,53.0,84.35,53.0,.80)]

# Protected VIN distribution based on actual v6 pad coordinates:
# D2.2=(79.65,53), D1.2=(77.5,60), C2.1=(67.5,53.975),
# C3.1 after rot270=(70,56.4), C1.1=(63.5,58.475), U6.2=(60.15,58.5).
r += [
    seg(3,79.65,53.0,77.8,53.0,.80),
    seg(3,77.8,53.0,77.5,60.0,.80),
    seg(3,79.65,53.0,72.0,53.0,.65),
    seg(3,72.0,53.0,67.5,53.975,.60),
    seg(3,67.5,53.975,69.0,55.2,.55),
    seg(3,69.0,55.2,70.0,56.4,.55),
    seg(3,70.0,56.4,66.5,56.4,.60),
    seg(3,66.5,56.4,63.5,58.475,.50),
    seg(3,63.5,58.475,60.15,58.5,.40),
]

# Buck switch loop. D5 after rot90: GND pad1=(56.2,50.9), SW pad2=(56.2,55.7).
r += [
    seg(7,55.85,59.0,53.5,59.0,.70),
    seg(7,55.85,59.0,56.2,55.7,.45),
    # bootstrap C4: pad2 SW=(57.5,61.5), pad1 BOOT=(58.5,61.5)
    seg(7,55.85,59.0,56.5,60.2,.25),
    seg(7,56.5,60.2,57.5,61.5,.25),
    seg(8,60.15,59.0,59.6,60.3,.25),
    seg(8,59.6,60.3,58.5,61.5,.25),
]

# 3V3 output from L1 to local output capacitors. Plane fanout below distributes globally.
r += [
    seg(9,47.5,59.0,46.0,59.0,.80),
    seg(9,46.0,59.0,46.0,55.5,.60), seg(9,46.0,55.5,44.475,55.5,.60),
    seg(9,46.0,59.0,46.0,60.5,.60), seg(9,46.0,60.5,44.475,60.5,.60),
]

# Parse pad coordinates after all placement changes for 3V3_MAIN fanout.
def pad_points(text, wanted_net=9):
    pts=[]
    for blk in blocks(text):
        rm=re.search(r'\(fp_text reference "([^"]+)"',blk)
        am=re.search(r'\n    \(at ([-0-9.]+) ([-0-9.]+)(?: ([-0-9.]+))?\)',blk)
        if not rm or not am: continue
        ref=rm.group(1); fx=float(am.group(1)); fy=float(am.group(2)); fr=float(am.group(3) or 0)
        th=math.radians(fr)
        for line in blk.splitlines():
            if '(pad "' not in line or f'(net {wanted_net} "3V3_MAIN")' not in line:
                continue
            pm=re.search(r'\(pad "([^"]*)" .*?\(at ([-0-9.]+) ([-0-9.]+)',line)
            if not pm: continue
            px=float(pm.group(2)); py=float(pm.group(3))
            gx=fx+px*math.cos(th)-py*math.sin(th)
            gy=fy+px*math.sin(th)+py*math.cos(th)
            pts.append((ref,pm.group(1),gx,gy,fx,fy))
    return pts

# Fanout every 3V3_MAIN island to the In2 plane. Outward placement reduces via/pad congestion.
seen=[]
for ref,pnum,gx,gy,fx,fy in pad_points(s,9):
    dx=gx-fx; dy=gy-fy; d=math.hypot(dx,dy)
    if d < 0.25:
        # central/thermal-style pad: choose a deterministic downward fanout
        ux,uy=0.0,1.0
    else:
        ux,uy=dx/d,dy/d
    dist=1.15
    vx=gx+ux*dist; vy=gy+uy*dist
    # Keep fanout vias inside the board; reverse direction if needed.
    if not (1.0 <= vx <= 99.0 and 1.0 <= vy <= 64.0):
        vx=gx-ux*dist; vy=gy-uy*dist
    # Avoid duplicate vias where same-net pads share nearly the same point.
    if any((vx-x)**2+(vy-y)**2 < 0.25 for x,y in seen):
        vx += 0.45; vy += 0.45
    seen.append((vx,vy))
    r.append(seg(9,gx,gy,vx,vy,.30))
    r.append(via(9,vx,vy,.70,.35))

# GND stitching vias link F/B pours to the solid inner GND reference plane.
for x,y in [(8,28),(18,28),(30,28),(42,28),(58,28),(72,28),(90,28),
            (8,54),(24,54),(36,54),(76,56),(92,56),(34,8),(62,8),(96,34)]:
    r.append(via(1,x,y,.80,.40))

zones=[
'''  (zone (net 1) (net_name "GND") (layer "F.Cu") (hatch edge 0.5)
    (connect_pads (clearance 0.25)) (min_thickness 0.25)
    (fill yes (thermal_gap 0.30) (thermal_bridge_width 0.30))
    (polygon (pts (xy 0.5 0.5) (xy 99.5 0.5) (xy 99.5 64.5) (xy 0.5 64.5))))''',
'''  (zone (net 1) (net_name "GND") (layer "B.Cu") (hatch edge 0.5)
    (connect_pads (clearance 0.25)) (min_thickness 0.25)
    (fill yes (thermal_gap 0.30) (thermal_bridge_width 0.30))
    (polygon (pts (xy 0.5 0.5) (xy 99.5 0.5) (xy 99.5 64.5) (xy 0.5 64.5))))''',
'''  (zone (net 9) (net_name "3V3_MAIN") (layer "In2.Cu") (hatch edge 0.5)
    (connect_pads (clearance 0.25)) (min_thickness 0.25)
    (fill yes (thermal_gap 0.30) (thermal_bridge_width 0.30))
    (polygon (pts (xy 0.8 0.8) (xy 99.2 0.8) (xy 99.2 64.2) (xy 0.8 64.2))))''',
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r+zones)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v6 planes, fanout and power cleanup to {P}')
