from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.B LTE UART LINK-side pass.
# TXU0202:
#   VCCA=LTE_1V8, VCCB=3V3_LINK
#   A1(pin5) <- modem TXD 1V8, B1Y(pin8) -> U2 RX 3V3
#   B2(pin1) <- U2 TX 3V3, A2Y(pin4) -> modem RXD 1V8
# ESP32-C6-WROOM-1 U2:
#   physical pin 17 = GPIO19 = LTE RX
#   physical pin 18 = GPIO20 = LTE TX
#
# Long runs use In2 only at the extreme right side. Horizontal runs are below
# the original 100x65 power-plane boundary so they do not split 3V3_MAIN.

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

# U10 high-side assignments.
a,b,u10=find_fp(s,'U10')
for pn,n in {
    1:'LTE_UART_TX_3V3', # B2 <- U2 TX
    7:'3V3_LINK',        # VCCB
    8:'LTE_UART_RX_3V3', # B1Y -> U2 RX
}.items():
    u10=assign_pad(u10,pn,n)
s=s[:a]+u10+s[b:]

# U2: GPIO19/GPIO20, both non-strapping pins.
a,b,u2=find_fp(s,'U2')
u2=assign_pad(u2,17,'LTE_UART_RX_3V3')
u2=assign_pad(u2,18,'LTE_UART_TX_3V3')
s=s[:a]+u2+s[b:]

# Refresh after pad edits.
_,_,u10=find_fp(s,'U10')
_,_,u2=find_fp(s,'U2')
p_u10_tx=pad_xy(u10,1)
p_u10_vccb=pad_xy(u10,7)
p_u10_rx=pad_xy(u10,8)
p_u2_rx=pad_xy(u2,17)
p_u2_tx=pad_xy(u2,18)

def fp0603(ref,val,x,y,rot,n1,n2):
    return f'''  (footprint "RevB:0603_LTE_LINK" (layer "F.Cu")
    (at {x:.3f} {y:.3f} {rot})
    (attr smd)
    (fp_text reference "{ref}" (at 0 -1.4 {rot}) (layer "F.SilkS") hide (effects (font (size .8 .8) (thickness .1))))
    (fp_text value "{val}" (at 0 1.3 {rot}) (layer "F.Fab") (effects (font (size .6 .6) (thickness .1))))
    (pad "1" smd roundrect (at -0.8 0 {rot}) (size .75 .95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n1,True)}{(' (zone_connect 2)' if n1=='GND' else '')})
    (pad "2" smd roundrect (at 0.8 0 {rot}) (size .75 .95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n2,True)}{(' (zone_connect 2)' if n2=='GND' else '')})
  )'''

# VCCB decoupling close to U10.
c74=fp0603('C74','100nF TXU0202 VCCB',52.4,73.5,0,'3V3_LINK','GND')
close=s.rfind(')')
s=s[:close]+'\n'+c74+'\n'+s[close:]

def seg(n,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'

def via(n,x,y,size=.60,drill=.30):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

r=[
    # RX: pad 17 -> far-right edge -> B.Cu -> lower LTE bay -> pin 8.
    # The x=99.1 / x=99.5 dogleg clears the USB shell and J4 through-hole pads.
    seg('LTE_UART_RX_3V3',p_u2_rx[0],p_u2_rx[1],99.10,p_u2_rx[1],.20),
    via('LTE_UART_RX_3V3',99.10,p_u2_rx[1],.60,.30),
    seg('LTE_UART_RX_3V3',99.10,p_u2_rx[1],99.10,40.0,.20,'B.Cu'),
    seg('LTE_UART_RX_3V3',99.10,40.0,99.50,40.0,.20,'B.Cu'),
    seg('LTE_UART_RX_3V3',99.50,40.0,99.50,55.0,.20,'B.Cu'),
    seg('LTE_UART_RX_3V3',99.50,55.0,99.10,55.0,.20,'B.Cu'),
    seg('LTE_UART_RX_3V3',99.10,55.0,99.10,66.0,.20,'B.Cu'),
    seg('LTE_UART_RX_3V3',99.10,66.0,55.0,66.0,.20,'B.Cu'),
    seg('LTE_UART_RX_3V3',55.0,66.0,55.0,70.75,.20,'B.Cu'),
    via('LTE_UART_RX_3V3',55.0,70.75,.60,.30),
    seg('LTE_UART_RX_3V3',55.0,70.75,p_u10_rx[0],p_u10_rx[1],.20),

    # TX: same perimeter on In2, then a short B.Cu drop outside the power stage.
    seg('LTE_UART_TX_3V3',p_u2_tx[0],p_u2_tx[1],99.10,p_u2_tx[1],.20),
    via('LTE_UART_TX_3V3',99.10,p_u2_tx[1],.60,.30),
    seg('LTE_UART_TX_3V3',99.10,p_u2_tx[1],99.10,40.0,.20,'In2.Cu'),
    seg('LTE_UART_TX_3V3',99.10,40.0,99.50,40.0,.20,'In2.Cu'),
    seg('LTE_UART_TX_3V3',99.50,40.0,99.50,55.0,.20,'In2.Cu'),
    seg('LTE_UART_TX_3V3',99.50,55.0,99.10,55.0,.20,'In2.Cu'),
    seg('LTE_UART_TX_3V3',99.10,55.0,99.10,67.0,.20,'In2.Cu'),
    seg('LTE_UART_TX_3V3',99.10,67.0,70.0,67.0,.20,'In2.Cu'),
    via('LTE_UART_TX_3V3',70.0,67.0,.60,.30),
    seg('LTE_UART_TX_3V3',70.0,67.0,70.0,71.25,.20,'B.Cu'),
    via('LTE_UART_TX_3V3',70.0,71.25,.60,.30),
    seg('LTE_UART_TX_3V3',70.0,71.25,70.0,70.75,.20),
    seg('LTE_UART_TX_3V3',70.0,70.75,p_u10_tx[0],p_u10_tx[1],.20),

    # VCCB: tap the existing 3V3_LINK via at 93.5/22.8 on F.Cu, use the same
    # safe perimeter, then enter In2 only after the old 100x65 board area.
    seg('3V3_LINK',93.50,22.80,99.10,22.80,.25),
    seg('3V3_LINK',99.10,22.80,99.10,40.0,.25),
    seg('3V3_LINK',99.10,40.0,99.50,40.0,.25),
    seg('3V3_LINK',99.50,40.0,99.50,55.0,.25),
    seg('3V3_LINK',99.50,55.0,99.10,55.0,.25),
    seg('3V3_LINK',99.10,55.0,99.10,68.0,.25),
    via('3V3_LINK',99.10,68.0,.60,.30),
    seg('3V3_LINK',99.10,68.0,51.60,68.0,.25,'In2.Cu'),

    # Pin 7 fan-out: one straight row into U10.
    seg('3V3_LINK',57.20,68.0,57.20,70.25,.22,'In2.Cu'),
    via('3V3_LINK',57.20,70.25,.60,.30),
    seg('3V3_LINK',57.20,70.25,p_u10_vccb[0],p_u10_vccb[1],.20),

    # C74 VCCB bypass branches from the same quiet In2 trunk.
    seg('3V3_LINK',51.60,68.0,51.60,73.50,.22,'In2.Cu'),
    via('3V3_LINK',51.60,73.50,.60,.30),
    seg('3V3_LINK',51.60,73.50,51.60,73.50,.20),
    seg('GND',53.20,73.50,54.20,73.50,.20),
    via('GND',54.20,73.50,.70,.35),
]
close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

# Postconditions.
_,_,u10c=find_fp(s,'U10')
_,_,u2c=find_fp(s,'U2')
for n in ('LTE_UART_RX_3V3','LTE_UART_TX_3V3','3V3_LINK'):
    if n not in u10c:
        raise RuntimeError(f'U10 missing {n}')
for n in ('LTE_UART_RX_3V3','LTE_UART_TX_3V3'):
    if n not in u2c:
        raise RuntimeError(f'U2 missing {n}')
if 'reference "C74"' not in s:
    raise RuntimeError('C74 missing')

P.write_text(s,encoding='utf-8')
print('Applied Rev.B LTE UART LINK-side 3.3V pass')
