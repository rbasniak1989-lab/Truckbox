from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.B LTE UART local-side pass.
# A7683E UART and VDD_EXT are 1.8 V. TXU0202 channel orientation:
#   A1(pin5, 1V8 input) -> B1Y(pin8, 3V3 output)  [modem TX]
#   B2(pin1, 3V3 input) -> A2Y(pin4, 1V8 output)  [modem RX]
# This pass closes only the modem/1V8 side first. The 3V3 MCU side follows
# in a separate pass so the Run-219 0/0 power/RF baseline remains debuggable.

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
    return (re.search(r'\\(property\\s+"Reference"\\s+"'+re.escape(ref)+r'"',block) or
            re.search(r'\\(fp_text\\s+reference\\s+"?'+re.escape(ref)+r'"?(?:\\s|\\))',block))

def find_fp(text,ref):
    for a,b,blk in iter_blocks(text,'footprint'):
        if ref_in_fp(blk,ref): return a,b,blk
    raise RuntimeError(f'footprint {ref} not found')

net_pairs=[(int(i),n) for i,n in re.findall(r'\\(net\\s+(\\d+)\\s+"([^"]+)"\\)',s)]
if not net_pairs: raise RuntimeError('numeric net table missing')
net_id={n:i for i,n in net_pairs}
new_nets=['LTE_1V8','LTE_UART_TX_1V8','LTE_UART_RX_1V8']
next_id=max(net_id.values())+1
adds=[]
for n in new_nets:
    if n not in net_id:
        net_id[n]=next_id; adds.append(f'  (net {next_id} "{n}")'); next_id+=1
if adds:
    m=list(re.finditer(r'^  \\(net \\d+ "[^"]+"\\)$',s,re.M))
    if not m: raise RuntimeError('net insertion point missing')
    pos=m[-1].end(); s=s[:pos]+'\\n'+'\\n'.join(adds)+s[pos:]

def ne(n,pad=False):
    return f'(net {net_id[n]} "{n}")' if pad else f'(net {net_id[n]})'

def assign_pad(block,pn,net):
    m=re.search(r'\\(pad\\s+"?'+re.escape(str(pn))+r'"?\\s+',block)
    if not m: raise RuntimeError(f'pad {pn} missing')
    j=balanced_block(block,m.start()); pb=block[m.start():j]
    if re.search(r'\\(net\\s+(?:\\d+\\s+)?"[^"]+"\\)',pb):
        pb=re.sub(r'\\(net\\s+(?:\\d+\\s+)?"[^"]+"\\)',ne(net,True),pb,count=1)
    else:
        pb=pb[:-1]+' '+ne(net,True)+')'
    return block[:m.start()]+pb+block[j:]

def fp_at(block):
    m=re.search(r'\\(at\\s+([-+0-9.]+)\\s+([-+0-9.]+)(?:\\s+([-+0-9.]+))?\\)',block)
    if not m: raise RuntimeError('footprint at missing')
    return float(m.group(1)),float(m.group(2)),float(m.group(3) or 0)

def pad_xy(block,pn):
    x,y,rot=fp_at(block)
    m=re.search(r'\\(pad\\s+"?'+re.escape(str(pn))+r'"?\\s+',block)
    if not m: raise RuntimeError(f'pad {pn} missing for xy')
    j=balanced_block(block,m.start()); pb=block[m.start():j]
    a=re.search(r'\\(at\\s+([-+0-9.]+)\\s+([-+0-9.]+)',pb)
    if not a: raise RuntimeError(f'pad {pn} local at missing')
    lx,ly=map(float,a.groups()); ang=math.radians(rot)
    return (x+lx*math.cos(ang)+ly*math.sin(ang),
            y-lx*math.sin(ang)+ly*math.cos(ang))

# A7683E: pin1 TXD, pin2 RXD, pin40 VDD_EXT 1.8V.
a,b,u9=find_fp(s,'U9')
for pn,n in {1:'LTE_UART_TX_1V8',2:'LTE_UART_RX_1V8',40:'LTE_1V8'}.items():
    u9=assign_pad(u9,pn,n)
s=s[:a]+u9+s[b:]

# TXU0202 modem-side pins only in this pass.
a,b,u10=find_fp(s,'U10')
for pn,n in {
    2:'GND',
    3:'LTE_1V8',       # VCCA
    4:'LTE_UART_RX_1V8', # A2Y -> modem RXD
    5:'LTE_UART_TX_1V8', # A1 <- modem TXD
    6:'LTE_1V8',       # OE high only when modem VDD_EXT is present
}.items():
    u10=assign_pad(u10,pn,n)
s=s[:a]+u10+s[b:]

_,_,u9=find_fp(s,'U9'); _,_,u10=find_fp(s,'U10')
p_tx=pad_xy(u9,1); p_rx=pad_xy(u9,2); p_1v8=pad_xy(u9,40)
p4=pad_xy(u10,4); p5=pad_xy(u10,5); p3=pad_xy(u10,3); p6=pad_xy(u10,6); p2=pad_xy(u10,2)

def fp0603(ref,val,x,y,rot,n1,n2):
    return f'''  (footprint "RevB:0603_LTE_IO" (layer "F.Cu")
    (at {x:.3f} {y:.3f} {rot})
    (attr smd)
    (fp_text reference "{ref}" (at 0 -1.4 {rot}) (layer "F.SilkS") hide (effects (font (size .8 .8) (thickness .1))))
    (fp_text value "{val}" (at 0 1.3 {rot}) (layer "F.Fab") (effects (font (size .6 .6) (thickness .1))))
    (pad "1" smd roundrect (at -0.8 0 {rot}) (size .75 .95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n1,True)}{(' (zone_connect 2)' if n1=='GND' else '')})
    (pad "2" smd roundrect (at 0.8 0 {rot}) (size .75 .95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n2,True)}{(' (zone_connect 2)' if n2=='GND' else '')})
  )'''

# VCCA decoupling close to U10.
part=fp0603('C73','100nF LTE 1V8',59.5,73.5,0,'LTE_1V8','GND')
close=s.rfind(')'); s=s[:close]+'\\n'+part+'\\n'+s[close:]

def seg(n,x1,y1,x2,y2,w=.22,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'
def via(n,x,y,size=.60,drill=.30):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

r=[
    # Modem TXD: escape below module, run in free B.Cu LTE bay, enter U10 pin5 from left.
    seg('LTE_UART_TX_1V8',p_tx[0],p_tx[1],p_tx[0],89.0,.20),
    via('LTE_UART_TX_1V8',p_tx[0],89.0),
    seg('LTE_UART_TX_1V8',p_tx[0],89.0,60.0,89.0,.22,'B.Cu'),
    seg('LTE_UART_TX_1V8',60.0,89.0,60.0,p5[1],.22,'B.Cu'),
    via('LTE_UART_TX_1V8',60.0,p5[1]),
    seg('LTE_UART_TX_1V8',60.0,p5[1],p5[0],p5[1],.20),

    # Modem RXD: separate B.Cu corridor, then approach U10 pin4 vertically from above.
    seg('LTE_UART_RX_1V8',p_rx[0],p_rx[1],p_rx[0],90.0,.20),
    via('LTE_UART_RX_1V8',p_rx[0],90.0),
    seg('LTE_UART_RX_1V8',p_rx[0],90.0,61.0,90.0,.22,'B.Cu'),
    seg('LTE_UART_RX_1V8',61.0,90.0,61.0,67.5,.22,'B.Cu'),
    seg('LTE_UART_RX_1V8',61.0,67.5,p4[0],67.5,.22,'B.Cu'),
    via('LTE_UART_RX_1V8',p4[0],67.5),
    seg('LTE_UART_RX_1V8',p4[0],67.5,p4[0],p4[1],.20),

    # VDD_EXT 1.8V: short front escape, then B.Cu up the left side of U10.
    seg('LTE_1V8',p_1v8[0],p_1v8[1],41.3,p_1v8[1],.25),
    seg('LTE_1V8',41.3,p_1v8[1],41.3,83.6,.25),
    via('LTE_1V8',41.3,83.6,.70,.35),
    seg('LTE_1V8',41.3,83.6,58.0,83.6,.30,'B.Cu'),
    seg('LTE_1V8',58.0,83.6,58.0,72.0,.30,'B.Cu'),
    via('LTE_1V8',58.0,72.0,.70,.35),
    seg('LTE_1V8',58.0,72.0,p6[0],p6[1],.25),

    # Tie OE and VCCA together above U10, clear of signal pins.
    seg('LTE_1V8',p6[0],p6[1],62.0,68.0,.25),
    seg('LTE_1V8',62.0,68.0,66.0,68.0,.25),
    seg('LTE_1V8',66.0,68.0,p3[0],p3[1],.25),

    # 1.8-V bypass and U10 GND.
    seg('LTE_1V8',58.0,72.0,58.7,73.5,.25),
    seg('GND',60.3,73.5,60.8,73.5,.25),
    via('GND',60.8,73.5,.70,.35),
    seg('GND',p2[0],p2[1],66.2,70.25,.25),
    via('GND',66.2,70.25,.70,.35),
]
close=s.rfind(')'); s=s[:close]+'\\n'+'\\n'.join(r)+'\\n'+s[close:]

# Postconditions.
for n in ('LTE_1V8','LTE_UART_TX_1V8','LTE_UART_RX_1V8'):
    if n not in s: raise RuntimeError(f'missing net {n}')
for ref in ('U9','U10','C73'):
    if f'reference "{ref}"' not in s and f'(property "Reference" "{ref}"' not in s:
        raise RuntimeError(f'missing {ref}')
_,_,u10c=find_fp(s,'U10')
for n in ('LTE_1V8','LTE_UART_TX_1V8','LTE_UART_RX_1V8','GND'):
    if n not in u10c: raise RuntimeError(f'U10 missing {n}')

P.write_text(s,encoding='utf-8')
print('Applied Rev.B LTE UART modem-side 1.8V level-shifter pass')
