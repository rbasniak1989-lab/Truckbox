from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.B A7683E PWRKEY pass.
# SIMCom: U9 pin 39 PWRKEY is active-low, internally pulled to VBAT.
# U2 physical pin 8 = GPIO0 drives an N-MOSFET open drain.
# Q50 = 2N7002 C8545, SOT-23: 1=G, 2=S, 3=D.
# R70 = 100k gate pulldown; R71 = 1k series in PWRKEY line.

def balanced_block(text,start):
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
            if depth==0:return j+1
    raise RuntimeError('unterminated s-expression')

def iter_blocks(text,token):
    i=0; needle='('+token
    while True:
        i=text.find(needle,i)
        if i<0:return
        j=balanced_block(text,i)
        yield i,j,text[i:j]
        i=j

def ref_in_fp(block,ref):
    return (re.search(r'\(property\s+"Reference"\s+"'+re.escape(ref)+r'"',block) or
            re.search(r'\(fp_text\s+reference\s+"?'+re.escape(ref)+r'"?(?:\s|\))',block))

def find_fp(text,ref):
    for a,b,blk in iter_blocks(text,'footprint'):
        if ref_in_fp(blk,ref): return a,b,blk
    raise RuntimeError(f'footprint {ref} not found')

net_pairs=[(int(i),n) for i,n in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)',s)]
if not net_pairs: raise RuntimeError('numeric net table missing')
net_id={n:i for i,n in net_pairs}
next_id=max(net_id.values())+1
adds=[]
for n in ('LTE_PWRKEY','LTE_PWRKEY_SW','LTE_PWRKEY_GPIO'):
    if n not in net_id:
        net_id[n]=next_id; adds.append(f'  (net {next_id} "{n}")'); next_id+=1
if adds:
    m=list(re.finditer(r'^  \(net \d+ "[^"]+"\)$',s,re.M))
    pos=m[-1].end()
    s=s[:pos]+'\n'+'\n'.join(adds)+s[pos:]

def ne(n,pad=False):
    return f'(net {net_id[n]} "{n}")' if pad else f'(net {net_id[n]})'

def assign_pad(block,pn,net):
    m=re.search(r'\(pad\s+"?'+re.escape(str(pn))+r'"?\s+',block)
    if not m: raise RuntimeError(f'pad {pn} missing')
    j=balanced_block(block,m.start()); pb=block[m.start():j]
    if re.search(r'\(net\s+(?:\d+\s+)?"[^"]+"\)',pb):
        pb=re.sub(r'\(net\s+(?:\d+\s+)?"[^"]+"\)',ne(net,True),pb,count=1)
    else:
        # KiCad-saved files often keep a numeric-only net in pads; replace it too.
        if re.search(r'\(net\s+\d+\)',pb):
            pb=re.sub(r'\(net\s+\d+\)',ne(net,True),pb,count=1)
        else:
            pb=pb[:-1]+' '+ne(net,True)+')'
    return block[:m.start()]+pb+block[j:]

def fp_at(block):
    m=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)(?:\s+([-+0-9.]+))?\)',block)
    return float(m.group(1)),float(m.group(2)),float(m.group(3) or 0)

def pad_xy(block,pn):
    x,y,rot=fp_at(block)
    m=re.search(r'\(pad\s+"?'+re.escape(str(pn))+r'"?\s+',block)
    if not m: raise RuntimeError(f'pad {pn} missing for xy')
    j=balanced_block(block,m.start()); pb=block[m.start():j]
    a=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)',pb)
    lx,ly=map(float,a.groups())
    ang=math.radians(rot)
    return (x+lx*math.cos(ang)+ly*math.sin(ang),
            y-lx*math.sin(ang)+ly*math.cos(ang))

# Assign modem PWRKEY and LINK GPIO0.
a,b,u9=find_fp(s,'U9'); u9=assign_pad(u9,39,'LTE_PWRKEY'); s=s[:a]+u9+s[b:]
a,b,u2=find_fp(s,'U2'); u2=assign_pad(u2,8,'LTE_PWRKEY_GPIO'); s=s[:a]+u2+s[b:]
_,_,u9=find_fp(s,'U9'); _,_,u2=find_fp(s,'U2')
p39=pad_xy(u9,39); p8=pad_xy(u2,8)

def fp0603(ref,val,x,y,rot,n1,n2):
    return f'''  (footprint "RevB:0603_PWRKEY" (layer "F.Cu")
    (at {x:.3f} {y:.3f} {rot})
    (attr smd)
    (fp_text reference "{ref}" (at 0 -1.4 {rot}) (layer "F.SilkS") hide (effects (font (size .8 .8) (thickness .1))))
    (fp_text value "{val}" (at 0 1.3 {rot}) (layer "F.Fab") (effects (font (size .6 .6) (thickness .1))))
    (pad "1" smd roundrect (at -0.8 0 {rot}) (size .75 .95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n1,True)})
    (pad "2" smd roundrect (at 0.8 0 {rot}) (size .75 .95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n2,True)})
  )'''

def fpsot23(ref,val,x,y):
    return f'''  (footprint "RevB:SOT23_PWRKEY" (layer "F.Cu")
    (at {x:.3f} {y:.3f} 0)
    (attr smd)
    (fp_text reference "{ref}" (at 0 -2.2) (layer "F.SilkS") hide (effects (font (size .8 .8) (thickness .1))))
    (fp_text value "{val}" (at 0 2.2) (layer "F.Fab") (effects (font (size .6 .6) (thickness .1))))
    (pad "1" smd roundrect (at -0.95 -0.95) (size 1.0 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .15) {ne('LTE_PWRKEY_GPIO',True)})
    (pad "2" smd roundrect (at -0.95 0.95) (size 1.0 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .15) {ne('GND',True)} (zone_connect 2))
    (pad "3" smd roundrect (at 0.95 0) (size 1.0 1.2) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .15) {ne('LTE_PWRKEY_SW',True)})
  )'''

# Compact local block just left of modem.
parts=[
    fpsot23('Q50','2N7002 C8545',36.0,81.10),
    fp0603('R70','100k PWRKEY GATE PD',34.0,80.95,90,'GND','LTE_PWRKEY_GPIO'),
    fp0603('R71','1k PWRKEY SERIES',39.5,81.10,0,'LTE_PWRKEY_SW','LTE_PWRKEY'),
]
close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(parts)+'\n'+s[close:]

def seg(n,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'
def via(n,x,y,size=.55,drill=.30):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

r=[
    # Local PWRKEY drain path: U9 pin39 -> R71 -> Q50 drain.
    seg('LTE_PWRKEY',p39[0],p39[1],40.30,81.10),
    seg('LTE_PWRKEY_SW',38.70,81.10,36.95,81.10),

    # Q50 source + R70 pulldown share a nearby GND via.
    seg('GND',35.05,82.05,34.00,81.75),
    seg('GND',34.00,81.75,33.40,82.40),
    via('GND',33.40,82.40,.65,.32),

    # Gate local endpoint.
    seg('LTE_PWRKEY_GPIO',34.00,80.15,35.05,80.15),
    seg('LTE_PWRKEY_GPIO',33.00,80.15,34.00,80.15),
    via('LTE_PWRKEY_GPIO',33.00,80.15),

    # U2 GPIO0 escape. Keep this above the two existing LTE UART lanes.
    seg('LTE_PWRKEY_GPIO',p8[0],p8[1],72.00,p8[1]),
    via('LTE_PWRKEY_GPIO',72.00,p8[1]),
    seg('LTE_PWRKEY_GPIO',72.00,p8[1],65.50,p8[1],.20,'In2.Cu'),
    seg('LTE_PWRKEY_GPIO',65.50,p8[1],65.50,23.00,.20,'In2.Cu'),
    via('LTE_PWRKEY_GPIO',65.50,23.00),

    # B.Cu lane centered between the existing x=64.5 and x=66.5 trunks.
    seg('LTE_PWRKEY_GPIO',65.50,23.00,65.50,32.20,.20,'B.Cu'),
    via('LTE_PWRKEY_GPIO',65.50,32.20),

    # Lower In2 leg hugs the existing RX cut so it does not create a new
    # independent 3V3_MAIN island.
    seg('LTE_PWRKEY_GPIO',65.50,32.20,63.50,32.20,.20,'In2.Cu'),
    seg('LTE_PWRKEY_GPIO',63.50,32.20,63.50,63.50,.20,'In2.Cu'),
    via('LTE_PWRKEY_GPIO',63.50,63.50),

    # B.Cu drops through the LTE bay, below the 3.8-V trunk at y=91.5,
    # then runs left and rises beside the SIM socket.
    seg('LTE_PWRKEY_GPIO',63.50,63.50,60.30,63.50,.20,'B.Cu'),
    seg('LTE_PWRKEY_GPIO',60.30,63.50,60.30,92.50,.20,'B.Cu'),
    seg('LTE_PWRKEY_GPIO',60.30,92.50,33.00,92.50,.20,'B.Cu'),
    seg('LTE_PWRKEY_GPIO',33.00,92.50,33.00,80.15,.20,'B.Cu'),
]

close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

# Postconditions.
_,_,u9c=find_fp(s,'U9'); _,_,u2c=find_fp(s,'U2')
if 'LTE_PWRKEY' not in u9c: raise RuntimeError('U9 PWRKEY assignment missing')
if 'LTE_PWRKEY_GPIO' not in u2c: raise RuntimeError('U2 GPIO0 assignment missing')
for ref in ('Q50','R70','R71'):
    if f'reference "{ref}"' not in s: raise RuntimeError(f'{ref} missing')

P.write_text(s,encoding='utf-8')
print('Applied Rev.B A7683E PWRKEY open-drain pass')
