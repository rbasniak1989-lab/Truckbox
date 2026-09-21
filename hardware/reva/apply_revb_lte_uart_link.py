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
s=s[:a]+u10+s[b:]

a,b,u2=find_fp(s,'U2')
u2=assign_pad(u2,11,'LTE_UART_RX_3V3')  # GPIO10
u2=assign_pad(u2,12,'LTE_UART_TX_3V3')  # GPIO11
s=s[:a]+u2+s[b:]

_,_,u10=find_fp(s,'U10')
_,_,u2=find_fp(s,'U2')
p8=pad_xy(u10,8)
p1=pad_xy(u10,1)
p11=pad_xy(u2,11)
p12=pad_xy(u2,12)

def seg(n,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'

def via(n,x,y,size=.55,drill=.30):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

r=[
    # RX: U2 GPIO10 -> TXU0202 B1Y(pin 8).
    # First short F.Cu escape from the left side of U2.
    seg('LTE_UART_RX_3V3',p11[0],p11[1],72.00,p11[1]),
    via('LTE_UART_RX_3V3',72.00,p11[1]),

    # In2 drops only to y=25.3, stopping before GNSS_PPS at y=25.8.
    seg('LTE_UART_RX_3V3',72.00,p11[1],72.00,25.30,.20,'In2.Cu'),
    via('LTE_UART_RX_3V3',72.00,25.30),

    # B.Cu passes the USB band, then returns to In2 below CAN_MODE.
    seg('LTE_UART_RX_3V3',72.00,25.30,72.00,32.20,.20,'B.Cu'),
    via('LTE_UART_RX_3V3',72.00,32.20),

    # Free In2 lower corridor to the U10 left-side fanout.
    seg('LTE_UART_RX_3V3',72.00,32.20,72.00,66.00,.20,'In2.Cu'),
    seg('LTE_UART_RX_3V3',72.00,66.00,61.00,66.00,.20,'In2.Cu'),
    seg('LTE_UART_RX_3V3',61.00,66.00,61.00,70.75,.20,'In2.Cu'),
    via('LTE_UART_RX_3V3',61.00,70.75),
    seg('LTE_UART_RX_3V3',61.00,70.75,p8[0],p8[1]),

    # TX: U2 GPIO11 -> TXU0202 B2(pin 1).
    seg('LTE_UART_TX_3V3',p12[0],p12[1],71.20,p12[1]),
    via('LTE_UART_TX_3V3',71.20,p12[1]),

    seg('LTE_UART_TX_3V3',71.20,p12[1],71.20,25.30,.20,'In2.Cu'),
    via('LTE_UART_TX_3V3',71.20,25.30),

    seg('LTE_UART_TX_3V3',71.20,25.30,71.20,32.20,.20,'B.Cu'),
    via('LTE_UART_TX_3V3',71.20,32.20),

    # Leave In2 before RX turns left, so the two UART nets never cross.
    seg('LTE_UART_TX_3V3',71.20,32.20,71.20,64.00,.20,'In2.Cu'),
    seg('LTE_UART_TX_3V3',71.20,64.00,67.00,64.00,.20,'In2.Cu'),
    via('LTE_UART_TX_3V3',67.00,64.00),

    # B.Cu vertical is clear here; return to F.Cu below U10 GND fanout.
    seg('LTE_UART_TX_3V3',67.00,64.00,67.00,71.30,.20,'B.Cu'),
    via('LTE_UART_TX_3V3',67.00,71.30),
    seg('LTE_UART_TX_3V3',67.00,71.30,67.00,70.75),
    seg('LTE_UART_TX_3V3',67.00,70.75,p1[0],p1[1]),
]

close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

# Strong postconditions: both endpoint pads assigned, no supply changes.
_,_,u10c=find_fp(s,'U10')
_,_,u2c=find_fp(s,'U2')
for n in ('LTE_UART_RX_3V3','LTE_UART_TX_3V3'):
    if n not in u10c or n not in u2c:
        raise RuntimeError(f'missing endpoint assignment for {n}')
if '3V3_LINK' in re.sub(r'\(net\s+\d+\s+"3V3_LINK"\)','',u10c):
    raise RuntimeError('signal-only pass unexpectedly touched U10 3V3_LINK')

P.write_text(s,encoding='utf-8')
print('Applied Rev.B LTE UART LINK signal-only pass: GPIO10/GPIO11')
