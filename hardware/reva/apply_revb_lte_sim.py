from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.B A7683E SIM1 staged routing — stage 4: complete VDD/CLK/DATA/RST.
# Run 262 froze VDD + CLK/R73 + DATA/R74 at 0 geometry/electrical and 0 unconnected.
# Official A7683E pins: 18 VDD, 17 RST, 16 CLK, 15 DATA.
# J5 contacts: C1 VCC, C2 RST, C3 CLK, C7 I/O.
# R72/R73/R74 are 22R series resistors on RST/CLK/DATA.

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
needed=('SIM_VDD','SIM_CLK_MOD','SIM_CLK_CARD','SIM_DATA_MOD','SIM_DATA_CARD','SIM_RST_MOD','SIM_RST_CARD')
adds=[]
next_id=max(net_id.values())+1
for name in needed:
    if name not in net_id:
        net_id[name]=next_id
        adds.append(f'  (net {next_id} "{name}")')
        next_id+=1
if adds:
    m=list(re.finditer(r'^  \(net \d+ "[^"]+"\)$',s,re.M))
    if not m: raise RuntimeError('net insertion point not found')
    pos=m[-1].end()
    s=s[:pos]+'\n'+'\n'.join(adds)+s[pos:]

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

# Assign only the endpoints enabled in this stage.
a,b,u9=find_fp(s,'U9')
u9=assign_pad(u9,18,'SIM_VDD')
u9=assign_pad(u9,17,'SIM_RST_MOD')
u9=assign_pad(u9,16,'SIM_CLK_MOD')
u9=assign_pad(u9,15,'SIM_DATA_MOD')
s=s[:a]+u9+s[b:]

a,b,j5=find_fp(s,'J5')
j5=assign_pad(j5,'C1','SIM_VDD')
j5=assign_pad(j5,'C2','SIM_RST_CARD')
j5=assign_pad(j5,'C3','SIM_CLK_CARD')
j5=assign_pad(j5,'C7','SIM_DATA_CARD')
s=s[:a]+j5+s[b:]

_,_,u9=find_fp(s,'U9')
_,_,j5=find_fp(s,'J5')
p18=pad_xy(u9,18)
p17=pad_xy(u9,17)
p16=pad_xy(u9,16)
p15=pad_xy(u9,15)
c1=pad_xy(j5,'C1')
c2=pad_xy(j5,'C2')
c3=pad_xy(j5,'C3')
c7=pad_xy(j5,'C7')

def seg(n,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'

def via(n,x,y,size=.70,drill=.35):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

def fp0603(ref,val,x,y,n1,n2):
    return f'''  (footprint "RevB:0603_SIM" (layer "F.Cu")
    (at {x:.3f} {y:.3f} 0)
    (attr smd)
    (fp_text reference "{ref}" (at 0 -1.35) (layer "F.SilkS") hide (effects (font (size .7 .7) (thickness .1))))
    (fp_text value "{val}" (at 0 1.25) (layer "F.Fab") (effects (font (size .55 .55) (thickness .08))))
    (pad "1" smd roundrect (at -0.8 0) (size .75 .95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n1,True)})
    (pad "2" smd roundrect (at 0.8 0) (size .75 .95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n2,True)})
  )'''

# Run 253 left one collision only: the SIM_VDD escape via at x=58.9
# intersected the long LTE_UART_RX_1V8 B.Cu vertical at x=59. Run 254
# approved this local B.Cu jog with 0 geometry/electrical violations.
uart_rx_old=seg('LTE_UART_RX_1V8',59.0,90.0,59.0,67.0,.20,'B.Cu')
uart_rx_new='\n'.join([
    seg('LTE_UART_RX_1V8',59.0,90.0,60.2,90.0,.20,'B.Cu'),
    seg('LTE_UART_RX_1V8',60.2,90.0,60.2,67.0,.20,'B.Cu'),
    seg('LTE_UART_RX_1V8',60.2,67.0,59.0,67.0,.20,'B.Cu'),
])
if uart_rx_old not in s:
    raise RuntimeError('LTE_UART_RX_1V8 vertical baseline segment not found')
s=s.replace(uart_rx_old,uart_rx_new,1)

# Series resistors: R72 is kept to the right of J5/PWRKEY bulk parts.
# Its pad 1 is MOD (left), pad 2 is CARD (right) to avoid the Run-251 short.
close=s.rfind(')')
parts=[
    fp0603('R72','22R SIM_RST',36.3,93.0,'SIM_RST_MOD','SIM_RST_CARD'),
    fp0603('R73','22R SIM_CLK',28.0,90.5,'SIM_CLK_CARD','SIM_CLK_MOD'),
    fp0603('R74','22R SIM_DATA',21.6,90.5,'SIM_DATA_CARD','SIM_DATA_MOD'),
]
s=s[:close]+'\n'+'\n'.join(parts)+'\n'+s[close:]

r=[
    # ---- Frozen SIM_VDD from Run 254 ----
    seg('SIM_VDD',p18[0],p18[1],58.90,p18[1]),
    via('SIM_VDD',58.90,p18[1],.60,.30),
    seg('SIM_VDD',58.90,p18[1],58.90,77.20,.20,'In2.Cu'),
    seg('SIM_VDD',58.90,77.20,32.00,77.20,.20,'In2.Cu'),
    seg('SIM_VDD',32.00,77.20,32.00,76.60,.20,'In2.Cu'),
    via('SIM_VDD',32.00,76.60,.60,.30),
    seg('SIM_VDD',32.00,76.60,c1[0],c1[1]),

    # ---- Stage 2: SIM_CLK module side ----
    # Run 257 mapped the remaining obstacles. Keep the pad-side via at x=59.4,
    # then jog to x=59.8 on In2 so the vertical misses the frozen VDD via.
    # Cross at y=73.3: above the LTE_3V8/GND via rows and left of 3V3_LINK.
    seg('SIM_CLK_MOD',p16[0],p16[1],59.40,p16[1]),
    via('SIM_CLK_MOD',59.40,p16[1],.50,.30),
    seg('SIM_CLK_MOD',59.40,p16[1],59.80,p16[1],.20,'In2.Cu'),
    seg('SIM_CLK_MOD',59.80,p16[1],59.80,73.30,.20,'In2.Cu'),
    seg('SIM_CLK_MOD',59.80,73.30,31.00,73.30,.20,'In2.Cu'),
    seg('SIM_CLK_MOD',31.00,73.30,31.00,89.20,.20,'In2.Cu'),
    via('SIM_CLK_MOD',31.00,89.20,.60,.30),
    seg('SIM_CLK_MOD',31.00,89.20,28.80,90.50),

    # ---- Stage 2: SIM_CLK card side ----
    # C3 is at x=30.9/y=82.54. Stay on x=26 in B.Cu under the socket,
    # then approach from the right-side escape point x=31.8.
    seg('SIM_CLK_CARD',27.20,90.50,26.00,90.50),
    via('SIM_CLK_CARD',26.00,90.50,.60,.30),
    seg('SIM_CLK_CARD',26.00,90.50,26.00,c3[1],.20,'B.Cu'),
    seg('SIM_CLK_CARD',26.00,c3[1],31.80,c3[1],.20,'B.Cu'),
    seg('SIM_CLK_CARD',31.80,c3[1],31.80,81.00,.20,'B.Cu'),
    via('SIM_CLK_CARD',31.80,81.00,.60,.30),
    seg('SIM_CLK_CARD',31.80,81.00,c3[0],c3[1]),

    # ---- Stage 3: SIM_DATA module side ----
    # Reuse the old clean U9 bottom escape, but keep DATA on In2 below the
    # frozen CLK/VDD corridors. y=91.3 clears the CLK card via at (26,90.5).
    seg('SIM_DATA_MOD',p15[0],p15[1],58.90,p15[1]),
    seg('SIM_DATA_MOD',58.90,p15[1],58.90,88.20),
    seg('SIM_DATA_MOD',58.90,88.20,57.00,88.20),
    via('SIM_DATA_MOD',57.00,88.20,.60,.30),
    seg('SIM_DATA_MOD',57.00,88.20,35.50,88.20,.20,'In2.Cu'),
    # Cross the PWRKEY wall and the C68 footprint region on B.Cu. Return to
    # In2 only at x=30, left of C68/GND and clear of the x=32.3 GND via.
    via('SIM_DATA_MOD',35.50,88.20,.60,.30),
    seg('SIM_DATA_MOD',35.50,88.20,30.00,88.20,.20,'B.Cu'),
    seg('SIM_DATA_MOD',30.00,88.20,30.00,91.30,.20,'B.Cu'),
    via('SIM_DATA_MOD',30.00,91.30,.60,.30),
    seg('SIM_DATA_MOD',30.00,91.30,23.20,91.30,.20,'In2.Cu'),
    via('SIM_DATA_MOD',23.20,91.30,.60,.30),
    seg('SIM_DATA_MOD',23.20,91.30,22.40,90.50),

    # ---- Stage 3: SIM_DATA card side ----
    # The historical C7 approach at x=18 did not produce DATA-card DRC errors.
    seg('SIM_DATA_CARD',20.80,90.50,20.00,91.30),
    via('SIM_DATA_CARD',20.00,91.30,.60,.30),
    seg('SIM_DATA_CARD',20.00,91.30,18.00,91.30,.20,'B.Cu'),
    seg('SIM_DATA_CARD',18.00,91.30,18.00,c7[1],.20,'B.Cu'),
    via('SIM_DATA_CARD',18.00,c7[1],.60,.30),
    seg('SIM_DATA_CARD',18.00,c7[1],c7[0],c7[1]),

    # ---- Stage 4: SIM_RST module side ----
    # Runs 263-267 mapped the x=36..39 corridor as occupied by PWRKEY,
    # D70/Q50 and C67/C68/LTE_3V8. Leave that corridor completely:
    # escape to the right of U9, travel on In1 below the LTE block, and
    # return to F.Cu left of C68 only for the short connection to R72.
    seg('SIM_RST_MOD',p17[0],p17[1],58.70,p17[1]),
    via('SIM_RST_MOD',58.70,p17[1],.60,.30),
    seg('SIM_RST_MOD',58.70,p17[1],58.70,94.20,.20,'In1.Cu'),
    seg('SIM_RST_MOD',58.70,94.20,31.80,94.20,.20,'In1.Cu'),
    seg('SIM_RST_MOD',31.80,94.20,31.80,93.00,.20,'In1.Cu'),
    via('SIM_RST_MOD',31.80,93.00,.60,.30),
    seg('SIM_RST_MOD',31.80,93.00,35.50,93.00),

    # ---- Stage 4: SIM_RST card side ----
    # R72 moved to y=93, clear of C68. Use the bottom B.Cu corridor at y=92.8,
    # then rise at x=27.5 and enter C2 on In2/F.Cu.
    seg('SIM_RST_CARD',37.10,93.00,38.00,93.60),
    via('SIM_RST_CARD',38.00,93.60,.60,.30),
    seg('SIM_RST_CARD',38.00,93.60,27.50,93.60,.20,'B.Cu'),
    seg('SIM_RST_CARD',27.50,93.60,27.50,83.40,.20,'B.Cu'),
    via('SIM_RST_CARD',27.50,83.40,.60,.30),
    seg('SIM_RST_CARD',27.50,83.40,27.50,c2[1],.20,'In2.Cu'),
    via('SIM_RST_CARD',27.50,c2[1],.60,.30),
    seg('SIM_RST_CARD',27.50,c2[1],c2[0],c2[1]),
]
close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

_,_,u9c=find_fp(s,'U9')
_,_,j5c=find_fp(s,'J5')
for n in ('SIM_VDD','SIM_RST_MOD','SIM_CLK_MOD','SIM_DATA_MOD'):
    if n not in u9c: raise RuntimeError(f'U9 missing {n}')
for n in ('SIM_VDD','SIM_RST_CARD','SIM_CLK_CARD','SIM_DATA_CARD'):
    if n not in j5c: raise RuntimeError(f'J5 missing {n}')
for ref in ('R72','R73','R74'):
    if f'reference "{ref}"' not in s: raise RuntimeError(f'{ref} missing')

P.write_text(s,encoding='utf-8')
print(f'Applied Rev.B SIM1 staged pass 4 complete: p17={p17}, c2={c2}; p16={p16}, c3={c3}; p15={p15}, c7={c7}')
