from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.B A7683E SIM1 staged routing — stage 1: SIM_VDD only.
# Baseline is Run 249 (0 geometry/electrical, 0 unconnected).
# Official A7683E pin 18 = SIM1_VDD; J5 C1 = SIM VCC.
# RST/CLK/DATA and their 22R resistors are deliberately NOT introduced yet.

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
if 'SIM_VDD' not in net_id:
    net_id['SIM_VDD']=max(net_id.values())+1
    m=list(re.finditer(r'^  \(net \d+ "[^"]+"\)$',s,re.M))
    if not m: raise RuntimeError('net insertion point not found')
    pos=m[-1].end()
    s=s[:pos]+f'\n  (net {net_id["SIM_VDD"]} "SIM_VDD")'+s[pos:]

def ne(n,pad=False):
    return f'(net {net_id[n]} "{n}")' if pad else f'(net {net_id[n]})'

def assign_pad(block,pn,net):
    m=re.search(r'\(pad\s+"?'+re.escape(str(pn))+r'"?\s+',block)
    if not m: raise RuntimeError(f'pad {pn} missing')
    j=balanced_block(block,m.start()); pb=block[m.start():j]
    if re.search(r'\(net\s+(?:\d+\s+)?"[^"]*"\)',pb):
        pb=re.sub(r'\(net\s+(?:\d+\s+)?"[^"]*"\)',ne(net,True),pb,count=1)
    elif re.search(r'\(net\s+\d+\)',pb):
        pb=re.sub(r'\(net\s+\d+\)',ne(net,True),pb,count=1)
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
    j=balanced_block(block,m.start()); pb=block[m.start():j]
    a=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)',pb)
    if not a: raise RuntimeError(f'pad {pn} at() missing')
    lx,ly=map(float,a.groups())
    ang=math.radians(rot)
    return (x+lx*math.cos(ang)+ly*math.sin(ang),
            y-lx*math.sin(ang)+ly*math.cos(ang))

# Assign only the supply endpoints.
a,b,u9=find_fp(s,'U9')
u9=assign_pad(u9,18,'SIM_VDD')
s=s[:a]+u9+s[b:]

a,b,j5=find_fp(s,'J5')
j5=assign_pad(j5,'C1','SIM_VDD')
s=s[:a]+j5+s[b:]

_,_,u9=find_fp(s,'U9')
_,_,j5=find_fp(s,'J5')
p18=pad_xy(u9,18)
c1=pad_xy(j5,'C1')

def seg(n,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'

def via(n,x,y,size=.70,drill=.35):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

# Run 253 left one collision only: the SIM_VDD escape via at x=58.9
# intersected the long LTE_UART_RX_1V8 B.Cu vertical at x=59. Keep the
# approved UART endpoints and make only a local B.Cu jog to x=60.2.
uart_rx_old=seg('LTE_UART_RX_1V8',59.0,90.0,59.0,67.0,.20,'B.Cu')
uart_rx_new='\n'.join([
    seg('LTE_UART_RX_1V8',59.0,90.0,60.2,90.0,.20,'B.Cu'),
    seg('LTE_UART_RX_1V8',60.2,90.0,60.2,67.0,.20,'B.Cu'),
    seg('LTE_UART_RX_1V8',60.2,67.0,59.0,67.0,.20,'B.Cu'),
])
if uart_rx_old not in s:
    raise RuntimeError('LTE_UART_RX_1V8 vertical baseline segment not found')
s=s.replace(uart_rx_old,uart_rx_new,1)

# Escape immediately from pin 18 to a small via beside the pad, instead of
# running along the U9 pad row. Run 252 proved that the y=73 F.Cu corridor
# crossed unassigned U9 pads 22/80. From there use In2 above the PWRKEY wall.
r=[
    seg('SIM_VDD',p18[0],p18[1],58.90,p18[1]),
    via('SIM_VDD',58.90,p18[1],.60,.30),

    seg('SIM_VDD',58.90,p18[1],58.90,77.20,.20,'In2.Cu'),
    seg('SIM_VDD',58.90,77.20,32.00,77.20,.20,'In2.Cu'),
    seg('SIM_VDD',32.00,77.20,32.00,76.60,.20,'In2.Cu'),
    via('SIM_VDD',32.00,76.60,.60,.30),
    seg('SIM_VDD',32.00,76.60,c1[0],c1[1]),
]
close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

_,_,u9c=find_fp(s,'U9')
_,_,j5c=find_fp(s,'J5')
if 'SIM_VDD' not in u9c: raise RuntimeError('U9 missing SIM_VDD')
if 'SIM_VDD' not in j5c: raise RuntimeError('J5 missing SIM_VDD')

P.write_text(s,encoding='utf-8')
print('Applied Rev.B SIM1 staged pass 1: SIM_VDD only')
