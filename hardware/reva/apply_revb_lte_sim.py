from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.B A7683E SIM1 functional pass.
# Official A7683E pins:
#   15 SIM1_DATA, 16 SIM1_CLK, 17 SIM1_RST, 18 SIM1_VDD
# J5 is GCT SIM8051-6-0-14-01-A / C3033025 with ISO contacts:
#   C1=VCC, C2=RST, C3=CLK, C5=GND, C6=VPP(NC), C7=I/O
# SIMCom reference: 22R series on RST/CLK/DATA; SIM_VDD direct.
# ESD and shunt capacitors are intentionally a later pass.

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
names=('SIM_VDD','SIM_RST_MOD','SIM_RST_CARD','SIM_CLK_MOD','SIM_CLK_CARD','SIM_DATA_MOD','SIM_DATA_CARD')
adds=[]
for n in names:
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
    lx,ly=map(float,a.groups())
    ang=math.radians(rot)
    return (x+lx*math.cos(ang)+ly*math.sin(ang),
            y-lx*math.sin(ang)+ly*math.cos(ang))

# Module endpoint assignments.
a,b,u9=find_fp(s,'U9')
for pn,n in {15:'SIM_DATA_MOD',16:'SIM_CLK_MOD',17:'SIM_RST_MOD',18:'SIM_VDD'}.items():
    u9=assign_pad(u9,pn,n)
s=s[:a]+u9+s[b:]

# Exact JLC nano-SIM contact assignments. C6/VPP intentionally left NC.
a,b,j5=find_fp(s,'J5')
for pn,n in {'C1':'SIM_VDD','C2':'SIM_RST_CARD','C3':'SIM_CLK_CARD',
             'C5':'GND','C7':'SIM_DATA_CARD','4':'GND','8':'GND'}.items():
    j5=assign_pad(j5,pn,n)
s=s[:a]+j5+s[b:]

_,_,u9=find_fp(s,'U9'); _,_,j5=find_fp(s,'J5')
p15=pad_xy(u9,15); p16=pad_xy(u9,16); p17=pad_xy(u9,17); p18=pad_xy(u9,18)
c1=pad_xy(j5,'C1'); c2=pad_xy(j5,'C2'); c3=pad_xy(j5,'C3'); c5=pad_xy(j5,'C5'); c7=pad_xy(j5,'C7')
sh4=pad_xy(j5,'4'); sh8=pad_xy(j5,'8')

def fp0603(ref,val,x,y,n1,n2):
    return f'''  (footprint "RevB:0603_SIM" (layer "F.Cu")
    (at {x:.3f} {y:.3f} 0)
    (attr smd)
    (fp_text reference "{ref}" (at 0 -1.35) (layer "F.SilkS") hide (effects (font (size .7 .7) (thickness .1))))
    (fp_text value "{val}" (at 0 1.25) (layer "F.Fab") (effects (font (size .55 .55) (thickness .08))))
    (pad "1" smd roundrect (at -0.8 0) (size .75 .95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n1,True)})
    (pad "2" smd roundrect (at 0.8 0) (size .75 .95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n2,True)})
  )'''

# Below the socket: clear of the SIM body and existing LTE bulk capacitors.
parts=[
    fp0603('R72','22R SIM_RST',28.0,90.5,'SIM_RST_CARD','SIM_RST_MOD'),
    fp0603('R73','22R SIM_CLK',24.8,90.5,'SIM_CLK_CARD','SIM_CLK_MOD'),
    fp0603('R74','22R SIM_DATA',21.6,90.5,'SIM_DATA_CARD','SIM_DATA_MOD'),
]
close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(parts)+'\n'+s[close:]

def seg(n,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'
def via(n,x,y,size=.70,drill=.35):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

r=[
    # ---- U9 top escape: SIM_VDD + SIM_RST ----
    # VDD is the upper pad, so it uses the inner x=58.9 lane; RST uses x=59.4.
    # This prevents either vertical lane from crossing the other pad's fanout.
    seg('SIM_VDD',p18[0],p18[1],58.90,p18[1]),
    seg('SIM_VDD',58.90,p18[1],58.90,68.80),
    seg('SIM_VDD',58.90,68.80,56.00,68.80),
    via('SIM_VDD',56.00,68.80),

    seg('SIM_RST_MOD',p17[0],p17[1],59.40,p17[1]),
    seg('SIM_RST_MOD',59.40,p17[1],59.40,69.50),
    seg('SIM_RST_MOD',59.40,69.50,55.00,69.50),
    via('SIM_RST_MOD',55.00,69.50),

    # Top corridors are above the PWRKEY wall and well clear of the LTE_3V8
    # via at (35.5,72.0).
    seg('SIM_VDD',56.00,68.80,27.50,68.80,.20,'In2.Cu'),
    seg('SIM_VDD',27.50,68.80,27.50,76.60,.20,'In2.Cu'),
    via('SIM_VDD',27.50,76.60),
    seg('SIM_VDD',27.50,76.60,c1[0],c1[1]),

    seg('SIM_RST_MOD',55.00,69.50,30.00,69.50,.20,'In2.Cu'),
    seg('SIM_RST_MOD',30.00,69.50,30.00,89.60,.20,'In2.Cu'),
    via('SIM_RST_MOD',30.00,89.60),
    seg('SIM_RST_MOD',30.00,89.60,28.80,90.50),

    # ---- U9 bottom escape: SIM_DATA + SIM_CLK ----
    # Both vias are below the module but above the existing UART B.Cu tracks.
    seg('SIM_DATA_MOD',p15[0],p15[1],58.90,p15[1]),
    seg('SIM_DATA_MOD',58.90,p15[1],58.90,88.00),
    seg('SIM_DATA_MOD',58.90,88.00,55.00,88.00),
    via('SIM_DATA_MOD',55.00,88.00),

    seg('SIM_CLK_MOD',p16[0],p16[1],59.40,p16[1]),
    seg('SIM_CLK_MOD',59.40,p16[1],59.40,88.40),
    seg('SIM_CLK_MOD',59.40,88.40,56.80,88.40),
    via('SIM_CLK_MOD',56.80,88.40),

    # DATA turns upward at x=36.0. CLK stays to its right (x=37.5), so the
    # two nets never cross. Both go over the PWRKEY wall at y<78.8 on In2.
    seg('SIM_DATA_MOD',55.00,88.00,36.00,88.00,.20,'In2.Cu'),
    seg('SIM_DATA_MOD',36.00,88.00,36.00,74.00,.20,'In2.Cu'),
    seg('SIM_DATA_MOD',36.00,74.00,22.40,74.00,.20,'In2.Cu'),
    seg('SIM_DATA_MOD',22.40,74.00,22.40,89.60,.20,'In2.Cu'),
    via('SIM_DATA_MOD',22.40,89.60),
    seg('SIM_DATA_MOD',22.40,89.60,22.40,90.50),

    seg('SIM_CLK_MOD',56.80,88.40,37.50,88.40,.20,'In2.Cu'),
    seg('SIM_CLK_MOD',37.50,88.40,37.50,73.00,.20,'In2.Cu'),
    seg('SIM_CLK_MOD',37.50,73.00,25.60,73.00,.20,'In2.Cu'),
    seg('SIM_CLK_MOD',25.60,73.00,25.60,89.60,.20,'In2.Cu'),
    via('SIM_CLK_MOD',25.60,89.60),
    seg('SIM_CLK_MOD',25.60,89.60,25.60,90.50),

    # ---- Card side after the 22R resistors ----
    # J5 has moved to x=20, opening a clean 5-mm routing channel on its right.
    seg('SIM_RST_CARD',27.20,90.50,29.00,90.50),
    via('SIM_RST_CARD',29.00,90.50),
    seg('SIM_RST_CARD',29.00,90.50,29.00,80.80,.20,'In2.Cu'),
    via('SIM_RST_CARD',29.00,80.80),
    seg('SIM_RST_CARD',29.00,80.80,c2[0],c2[1]),

    seg('SIM_CLK_CARD',24.00,90.50,24.00,91.40),
    via('SIM_CLK_CARD',24.00,91.40),
    seg('SIM_CLK_CARD',24.00,91.40,27.50,91.40,.20,'In2.Cu'),
    seg('SIM_CLK_CARD',27.50,91.40,27.50,83.30,.20,'In2.Cu'),
    via('SIM_CLK_CARD',27.50,83.30),
    seg('SIM_CLK_CARD',27.50,83.30,c3[0],c3[1]),

    seg('SIM_DATA_CARD',20.80,90.50,20.80,91.40),
    via('SIM_DATA_CARD',20.80,91.40),
    seg('SIM_DATA_CARD',20.80,91.40,12.80,91.40,.20,'In2.Cu'),
    seg('SIM_DATA_CARD',12.80,91.40,12.80,83.20,.20,'In2.Cu'),
    via('SIM_DATA_CARD',12.80,83.20),
    seg('SIM_DATA_CARD',12.80,83.20,c7[0],c7[1]),

    # Socket ground contact and both shell tabs, now comfortably left of LTE bulk.
    seg('GND',c5[0],c5[1],12.80,c5[1],.30),
    via('GND',12.80,c5[1],.70,.35),
    seg('GND',sh4[0],sh4[1],18.50,sh4[1],.30),
    via('GND',18.50,sh4[1],.70,.35),
    seg('GND',sh8[0],sh8[1],18.50,sh8[1],.30),
    via('GND',18.50,sh8[1],.70,.35),
]
close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

# Postconditions.
_,_,u9c=find_fp(s,'U9'); _,_,j5c=find_fp(s,'J5')
for n in ('SIM_VDD','SIM_RST_MOD','SIM_CLK_MOD','SIM_DATA_MOD'):
    if n not in u9c: raise RuntimeError(f'U9 missing {n}')
for n in ('SIM_VDD','SIM_RST_CARD','SIM_CLK_CARD','SIM_DATA_CARD','GND'):
    if n not in j5c: raise RuntimeError(f'J5 missing {n}')
for ref in ('R72','R73','R74'):
    if f'reference "{ref}"' not in s: raise RuntimeError(f'{ref} missing')

P.write_text(s,encoding='utf-8')
print('Applied Rev.B SIM1 functional pass: VDD + 22R RST/CLK/DATA')
