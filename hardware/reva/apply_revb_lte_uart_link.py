from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.B LTE UART: LINK-side signal-only validation pass.
# Deliberately based on the clean Run 226 modem-side baseline.
#
# ESP32-C6-WROOM-1 U2:
#   physical pin 11 = GPIO10 = LTE RX
#   physical pin 12 = GPIO11 = LTE TX
#
# TXU0202:
#   pin 8 B1Y -> ESP RX
#   pin 1 B2  <- ESP TX
#
# VCCB/3V3_LINK is intentionally NOT added in this pass. First prove the two
# signal corridors at DRC 0/0, then add the supply/decoupling separately.

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

# Numeric net table exists before KiCad re-save; support it here.
net_pairs=[(int(i),n) for i,n in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)',s)]
if not net_pairs:
    raise RuntimeError('numeric net table missing')
net_id={n:i for i,n in net_pairs}
next_id=max(net_id.values())+1
adds=[]
for n in ('LTE_UART_RX_3V3','LTE_UART_TX_3V3'):
    if n not in net_id:
        net_id[n]=next_id
        adds.append(f'  (net {next_id} "{n}")')
        next_id+=1
if adds:
    m=list(re.finditer(r'^  \(net \d+ "[^"]+"\)$',s,re.M))
    if not m: raise RuntimeError('net insertion point missing')
    pos=m[-1].end()
    s=s[:pos]+'\n'+'\n'.join(adds)+s[pos:]

def ne(n,pad=False):
    return f'(net {net_id[n]} "{n}")' if pad else f'(net {net_id[n]})'

def assign_pad(block,pn,net):
    m=re.search(r'\(pad\s+"?'+re.escape(str(pn))+r'"?\s+',block)
    if not m: raise RuntimeError(f'pad {pn} missing')
    j=balanced_block(block,m.start())
    pb=block[m.start():j]
    if re.search(r'\(net\s+(?:\d+\s+)?"[^"]+"\)',pb):
        pb=re.sub(r'\(net\s+(?:\d+\s+)?"[^"]+"\)',ne(net,True),pb,count=1)
    else:
        pb=pb[:-1]+' '+ne(net,True)+')'
    return block[:m.start()]+pb+block[j:]

def fp_at(block):
    m=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)(?:\s+([-+0-9.]+))?\)',block)
    if not m: raise RuntimeError('footprint at missing')
    return float(m.group(1)),float(m.group(2)),float(m.group(3) or 0)

def pad_xy(block,pn):
    x,y,rot=fp_at(block)
    m=re.search(r'\(pad\s+"?'+re.escape(str(pn))+r'"?\s+',block)
    if not m: raise RuntimeError(f'pad {pn} missing for xy')
    j=balanced_block(block,m.start())
    pb=block[m.start():j]
    a=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)',pb)
    if not a: raise RuntimeError(f'pad {pn} local at missing')
    lx,ly=map(float,a.groups())
    ang=math.radians(rot)
    return (x+lx*math.cos(ang)+ly*math.sin(ang),
            y-lx*math.sin(ang)+ly*math.cos(ang))

# Assign only the two signal channels.
a,b,u10=find_fp(s,'U10')
u10=assign_pad(u10,8,'LTE_UART_RX_3V3')
u10=assign_pad(u10,1,'LTE_UART_TX_3V3')
u10=assign_pad(u10,7,'3V3_LINK')
s=s[:a]+u10+s[b:]

a,b,u2=find_fp(s,'U2')
u2=assign_pad(u2,11,'LTE_UART_RX_3V3')  # GPIO10
u2=assign_pad(u2,12,'LTE_UART_TX_3V3')  # GPIO11
s=s[:a]+u2+s[b:]

_,_,u10=find_fp(s,'U10')
_,_,u2=find_fp(s,'U2')
p8=pad_xy(u10,8)
p1=pad_xy(u10,1)
p7=pad_xy(u10,7)
p11=pad_xy(u2,11)
p12=pad_xy(u2,12)

def seg(n,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'

def via(n,x,y,size=.55,drill=.30):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

r=[
    # RX: U2 GPIO10 -> TXU0202 B1Y(pin 8).
    seg('LTE_UART_RX_3V3',p11[0],p11[1],72.00,p11[1]),
    via('LTE_UART_RX_3V3',72.00,p11[1]),

    # Upper crossing uses In2 only above the GNSS bands.
    seg('LTE_UART_RX_3V3',72.00,p11[1],67.50,24.10,.20,'In2.Cu'),
    via('LTE_UART_RX_3V3',67.50,24.10),
    seg('LTE_UART_RX_3V3',67.50,24.10,67.50,32.20,.20,'B.Cu'),
    via('LTE_UART_RX_3V3',67.50,32.20),

    # Shift left to x=64 before descending; this clears the J1708_DE and
    # 3V3_MAIN through-vias around x=67..68.
    seg('LTE_UART_RX_3V3',67.50,32.20,64.00,32.20,.20,'In2.Cu'),
    seg('LTE_UART_RX_3V3',64.00,32.20,64.00,64.00,.20,'In2.Cu'),
    seg('LTE_UART_RX_3V3',64.00,64.00,61.00,64.00,.20,'In2.Cu'),
    seg('LTE_UART_RX_3V3',61.00,64.00,61.00,70.75,.20,'In2.Cu'),
    via('LTE_UART_RX_3V3',61.00,70.75),
    seg('LTE_UART_RX_3V3',61.00,70.75,p8[0],p8[1]),

    # TX: U2 GPIO11 -> TXU0202 B2(pin 1).
    # The initial via moves 0.2 mm right to clear the RX diagonal.
    seg('LTE_UART_TX_3V3',p12[0],p12[1],71.40,p12[1]),
    via('LTE_UART_TX_3V3',71.40,p12[1]),
    seg('LTE_UART_TX_3V3',71.40,p12[1],68.50,24.10,.20,'In2.Cu'),
    via('LTE_UART_TX_3V3',68.50,24.10),
    seg('LTE_UART_TX_3V3',68.50,24.10,68.50,32.20,.20,'B.Cu'),
    via('LTE_UART_TX_3V3',68.50,32.20),

    # Shift right to x=69 for the long lower descent.
    seg('LTE_UART_TX_3V3',68.50,32.20,69.00,32.20,.20,'In2.Cu'),
    seg('LTE_UART_TX_3V3',69.00,32.20,69.00,66.00,.20,'In2.Cu'),
    seg('LTE_UART_TX_3V3',69.00,66.00,67.00,66.00,.20,'In2.Cu'),
    via('LTE_UART_TX_3V3',67.00,66.00),
    seg('LTE_UART_TX_3V3',67.00,66.00,67.00,71.30,.20,'B.Cu'),
    via('LTE_UART_TX_3V3',67.00,71.30),
    seg('LTE_UART_TX_3V3',67.00,71.30,67.00,70.75),
    seg('LTE_UART_TX_3V3',67.00,70.75,p1[0],p1[1]),

    # The two narrow UART cuts in In2 can split the 3V3_MAIN pour.
    # Bridge the two already-existing 3V3_MAIN vias on B.Cu; the corridor
    # has >0.49 mm clearance to the nearest foreign B.Cu feature.
    seg('3V3_MAIN',65.40,41.20,68.00,43.135,.28,'B.Cu'),

    # Second bridge reconnects the 3V3_MAIN island left of the x=64 UART cut.
    # Detour below the J1708_RX vertical/horizontal copper; minimum checked
    # clearance to the J1708_RX via is ~0.21 mm.
    seg('3V3_MAIN',63.20,39.00,63.20,43.20,.28,'B.Cu'),
    seg('3V3_MAIN',63.20,43.20,66.50,43.20,.28,'B.Cu'),
    seg('3V3_MAIN',66.50,43.20,66.50,41.20,.28,'B.Cu'),
    seg('3V3_MAIN',66.50,41.20,65.40,41.20,.28,'B.Cu'),

    # VCCB supply for TXU0202 pin 7.
    # Start from the existing 3V3_LINK via at 93.5/22.8 and follow the outer
    # B.Cu edge corridor. x=99.25 keeps >0.5 mm copper-to-edge clearance and
    # clears the right-hand J4 through-hole column.
    seg('3V3_LINK',93.50,22.80,99.25,22.80,.20,'B.Cu'),
    seg('3V3_LINK',99.25,22.80,99.25,64.00,.20,'B.Cu'),
    seg('3V3_LINK',99.25,64.00,99.20,64.00,.20,'B.Cu'),
    via('3V3_LINK',99.20,64.00),

    # Below the original 65-mm board area, In2 is free of the 3V3_MAIN pour.
    seg('3V3_LINK',99.20,64.00,99.20,71.40,.20,'In2.Cu'),
    seg('3V3_LINK',99.20,71.40,60.60,71.40,.20,'In2.Cu'),
    via('3V3_LINK',60.60,71.40),

    # Local pin-7 fanout: remain above RX(pin 8) until x=60.6, then descend.
    seg('3V3_LINK',p7[0],p7[1],60.60,p7[1],.20),
    seg('3V3_LINK',60.60,p7[1],60.60,71.40,.20),
]

close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

# Strong postconditions: both UART endpoints plus VCCB assigned.
_,_,u10c=find_fp(s,'U10')
_,_,u2c=find_fp(s,'U2')
for n in ('LTE_UART_RX_3V3','LTE_UART_TX_3V3'):
    if n not in u10c or n not in u2c:
        raise RuntimeError(f'missing endpoint assignment for {n}')
if '3V3_LINK' not in u10c:
    raise RuntimeError('U10 VCCB missing 3V3_LINK')

P.write_text(s,encoding='utf-8')
print('Applied Rev.B LTE UART LINK pass: GPIO10/GPIO11 + VCCB')
