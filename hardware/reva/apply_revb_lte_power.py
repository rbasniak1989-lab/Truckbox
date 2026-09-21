from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.B LTE power pass.
# TPS54360B-Q1: 24-V vehicle rail -> 3.8-V LTE rail.
# Design target: 400 kHz, 22 uH, >=1 A continuous.
# A7683E local VBAT bulk follows SIMCom guidance (>=200 uF total).

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
new_nets=['LTE_SW','LTE_BOOT','LTE_FB','LTE_COMP','LTE_COMP_RC','LTE_EN']
next_id=max(net_id.values())+1
adds=[]
for n in new_nets:
    if n not in net_id:
        net_id[n]=next_id; adds.append(f'  (net {next_id} "{n}")'); next_id+=1
if adds:
    m=list(re.finditer(r'^  \(net \d+ "[^"]+"\)$',s,re.M))
    if not m: raise RuntimeError('net insertion point missing')
    pos=m[-1].end(); s=s[:pos]+'\n'+'\n'.join(adds)+s[pos:]

def ne(n,pad=False):
    return f'(net {net_id[n]} "{n}")' if pad else f'(net {net_id[n]})'

def assign_pad(block,pn,net):
    m=re.search(r'\(pad\s+"?'+re.escape(str(pn))+r'"?\s+',block)
    if not m: raise RuntimeError(f'pad {pn} missing')
    j=balanced_block(block,m.start()); pb=block[m.start():j]
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
    j=balanced_block(block,m.start()); pb=block[m.start():j]
    a=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)',pb)
    if not a: raise RuntimeError(f'pad {pn} local at missing')
    lx,ly=map(float,a.groups()); ang=math.radians(rot)
    return (x+lx*math.cos(ang)+ly*math.sin(ang),
            y-lx*math.sin(ang)+ly*math.cos(ang))

# Assign TPS54360B-Q1.
a,b,u11=find_fp(s,'U11')
for pn,n in {1:'LTE_BOOT',2:'VIN_PROT',3:'LTE_EN',4:'LTE_FB',5:'LTE_FB',6:'LTE_COMP',7:'GND',8:'LTE_SW',9:'GND'}.items():
    # pin 4 will be corrected to LTE_RT below after net is added lazily
    pass
# Add LTE_RT after U11 lookup so older builds remain deterministic.
if 'LTE_RT' not in net_id:
    m=list(re.finditer(r'^  \(net \d+ "[^"]+"\)$',s,re.M))
    nid=max(net_id.values())+1; net_id['LTE_RT']=nid
    pos=m[-1].end(); s=s[:pos]+f'\n  (net {nid} "LTE_RT")'+s[pos:]
    a,b,u11=find_fp(s,'U11')
for pn,n in {1:'LTE_BOOT',2:'VIN_PROT',3:'LTE_EN',4:'LTE_RT',5:'LTE_FB',6:'LTE_COMP',7:'GND',8:'LTE_SW',9:'GND'}.items():
    u11=assign_pad(u11,pn,n)
s=s[:a]+u11+s[b:]

# Refresh coordinates after net edits.
_,_,u11=find_fp(s,'U11'); _,_,u9=find_fp(s,'U9'); _,_,d2=find_fp(s,'D2')
p_boot=pad_xy(u11,1); p_vin=pad_xy(u11,2); p_en=pad_xy(u11,3); p_rt=pad_xy(u11,4)
p_fb=pad_xy(u11,5); p_comp=pad_xy(u11,6); p_gnd=pad_xy(u11,7); p_sw=pad_xy(u11,8); p_ep=pad_xy(u11,9)
p34=pad_xy(u9,34); p35=pad_xy(u9,35)
# D2 pad 2 is the protected VIN_PROT rail in Rev.A.
p_vinsrc=pad_xy(d2,2)

def fp2(ref,val,x,y,rot,n1,n2,kind='0603'):
    if kind=='0603': dx=.80; psx=.75; psy=.95
    elif kind=='1210': dx=1.70; psx=1.50; psy=2.60
    elif kind=='7343': dx=2.70; psx=2.50; psy=4.20
    # Exact JLC C14651 SS56B footprint SMB_L4.6-W3.6-LS5.3-RD.
    elif kind=='SMB': dx=2.36; psx=2.047; psy=2.192
    else: raise RuntimeError(kind)
    return f'''  (footprint "RevB:{kind}_{ref}" (layer "F.Cu")
    (at {x:.3f} {y:.3f} {rot})
    (attr smd)
    (fp_text reference "{ref}" (at 0 -2.0 {rot}) (layer "F.SilkS") hide (effects (font (size .8 .8) (thickness .1))))
    (fp_text value "{val}" (at 0 2.0 {rot}) (layer "F.Fab") (effects (font (size .6 .6) (thickness .1))))
    (pad "1" smd roundrect (at {-dx:.3f} 0 {rot}) (size {psx:.3f} {psy:.3f}) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n1,True)}{(' (zone_connect 2)' if n1=='GND' else '')})
    (pad "2" smd roundrect (at {dx:.3f} 0 {rot}) (size {psx:.3f} {psy:.3f}) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .18) {ne(n2,True)}{(' (zone_connect 2)' if n2=='GND' else '')})
  )'''

def ind(ref,val,x,y,n1,n2):
    # Exact JLC C19947701 / Bourns SRP7050WA footprint:
    # IND-SMD_L7.9-W7.3_SRP7050WA, pads +/-3.00 mm, 3.00 x 3.50 mm.
    return f'''  (footprint "RevB:IND-SMD_L7.9-W7.3_SRP7050WA" (layer "F.Cu")
    (at {x:.3f} {y:.3f})
    (attr smd)
    (fp_text reference "{ref}" (at 0 -4.2) (layer "F.SilkS") hide (effects (font (size .8 .8) (thickness .1))))
    (fp_text value "{val}" (at 0 4.2) (layer "F.Fab") (effects (font (size .7 .7) (thickness .1))))
    (pad "1" smd roundrect (at -3.0 0) (size 3.0 3.5) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .12) {ne(n1,True)})
    (pad "2" smd roundrect (at 3.0 0) (size 3.0 3.5) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .12) {ne(n2,True)})
  )'''

parts=[
    # Power stage. Keep the high-current loop compact around U11.
    ind('L50','SRP7050WA-220M 22uH 5A C19947701',70.0,75.0,'LTE_3V8','LTE_SW'),
    fp2('D50','SS56B 60V 5A C14651',72.0,68.5,180,'LTE_SW','GND','SMB'),
    fp2('C60','100nF BOOT',73.5,78.8,180,'LTE_BOOT','LTE_SW','0603'),

    # Input / output ceramic bulk, rotated so power and ground pads do not face each other.
    fp2('C61','2.2uF 50V VIN',82.0,83.0,90,'VIN_PROT','GND','1210'),
    fp2('C62','2.2uF 50V VIN',88.0,83.0,90,'VIN_PROT','GND','1210'),
    fp2('C63','47uF 10V OUT',69.0,82.0,90,'LTE_3V8','GND','1210'),
    fp2('C64','47uF 10V OUT',74.0,82.0,90,'LTE_3V8','GND','1210'),

    # 400-kHz programming, 3.8-V feedback and compensation.
    fp2('R60','243k RT 400kHz',92.0,82.0,0,'LTE_RT','GND','0603'),
    fp2('R61','38.3k FB HIGH',84.0,69.5,0,'LTE_FB','LTE_3V8','0603'),
    fp2('R62','10.2k FB LOW',84.0,72.0,0,'LTE_FB','GND','0603'),
    fp2('R63','6.34k COMP',84.0,65.8,0,'LTE_COMP','LTE_COMP_RC','0603'),
    fp2('C65','56nF COMP',88.0,65.8,0,'LTE_COMP_RC','GND','0603'),
    fp2('C66','100pF COMP POLE',84.0,67.8,0,'LTE_COMP','GND','0603'),

    # Local UVLO / enable: ~9.2-V turn-on threshold from protected VIN.
    fp2('R64','100k EN HIGH',85.0,88.0,0,'LTE_EN','VIN_PROT','0603'),
    fp2('R65','15k EN LOW',81.0,90.0,0,'LTE_EN','GND','0603'),

    # A7683E local VBAT reservoir / RF decoupling.
    # 180-deg rotation puts LTE_3V8 pads toward the modem/trunk and GND outward.
    fp2('C67','100uF 10V VBAT',36.0,84.0,180,'LTE_3V8','GND','7343'),
    fp2('C68','100uF 10V VBAT',36.0,90.0,180,'LTE_3V8','GND','7343'),
    fp2('C69','1uF VBAT',38.0,74.2,180,'LTE_3V8','GND','0603'),
    fp2('C70','100nF VBAT',38.0,76.4,180,'LTE_3V8','GND','0603'),
    fp2('C71','33pF VBAT',38.0,78.6,180,'LTE_3V8','GND','0603'),
    fp2('C72','10pF VBAT',34.0,72.0,180,'LTE_3V8','GND','0603'),
]
close=s.rfind(')'); s=s[:close]+'\n'+'\n'.join(parts)+'\n'+s[close:]

def seg(n,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'
def via(n,x,y,size=.75,drill=.35):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

# Component pad centers from the generic footprints above.
# L50: out=(67.8,68.3), SW=(74.2,68.3)
r=[
    # Protected 24-V input. Drop to B.Cu after D2 and run through the empty LTE bay.
    seg('VIN_PROT',p_vinsrc[0],p_vinsrc[1],77.000,52.000,.80,'F.Cu'),
    via('VIN_PROT',77.000,52.000,.90,.45),
    seg('VIN_PROT',77.000,52.000,93.0,65.0,1.00,'B.Cu'),
    seg('VIN_PROT',93.0,65.0,93.0,84.7,1.00,'B.Cu'),
    seg('VIN_PROT',93.0,84.7,89.2,84.7,1.00,'B.Cu'),
    via('VIN_PROT',89.2,84.7,.90,.45),
    seg('VIN_PROT',89.2,84.7,88.0,84.7,.80),
    seg('VIN_PROT',88.0,84.7,82.0,84.7,.80),
    via('VIN_PROT',80.5,84.7,.90,.45),
    seg('VIN_PROT',80.5,84.7,82.0,84.7,.80),
    seg('VIN_PROT',p_vin[0],p_vin[1],p_vin[0],80.3,.70),
    via('VIN_PROT',p_vin[0],80.3,.85,.40),
    seg('VIN_PROT',p_vin[0],80.3,80.5,84.7,.70,'B.Cu'),
    seg('VIN_PROT',85.8,88.0,86.6,88.0,.25),
    via('VIN_PROT',86.6,88.0,.70,.35),
    seg('VIN_PROT',86.6,88.0,80.5,84.7,.30,'B.Cu'),

    # Compact switch node: U11 SW, catch diode, inductor and bootstrap.
    seg('LTE_SW',p_sw[0],p_sw[1],74.2,70.5,.80),
    seg('LTE_SW',74.2,70.5,74.36,68.5,.80),
    seg('LTE_SW',74.2,70.5,73.0,75.0,.80),
    seg('LTE_SW',73.0,75.0,72.7,78.8,.35),
    seg('LTE_BOOT',p_boot[0],p_boot[1],74.3,78.8,.30),

    # Catch-diode ground.
    seg('GND',69.64,68.5,68.5,68.5,.60),
    via('GND',68.5,68.5,.85,.40),

    # 3.8-V output: short F.Cu neck, then a 2-mm B.Cu trunk around the modem.
    seg('LTE_3V8',67.0,75.0,65.5,76.0,1.20),
    via('LTE_3V8',65.5,76.0,1.00,.50),
    seg('LTE_3V8',65.5,76.0,65.5,91.5,2.00,'B.Cu'),
    seg('LTE_3V8',65.5,91.5,39.5,91.5,2.00,'B.Cu'),
    seg('LTE_3V8',39.5,91.5,39.5,74.2,2.00,'B.Cu'),
    via('LTE_3V8',39.5,76.15,1.00,.50),
    seg('LTE_3V8',39.5,76.15,41.0,76.15,1.20),
    seg('LTE_3V8',41.0,76.15,p34[0],p34[1],1.00),
    seg('LTE_3V8',p34[0],p34[1],p35[0],p35[1],1.00),

    # Output caps join the B.Cu trunk individually.
    seg('LTE_3V8',69.0,83.7,65.5,83.7,.90),
    via('LTE_3V8',65.5,83.7,.85,.40),
    seg('LTE_3V8',74.0,83.7,65.5,87.0,.90),
    via('LTE_3V8',65.5,87.0,.85,.40),
    seg('GND',69.0,80.3,67.5,80.3,.60), via('GND',67.5,80.3,.85,.40),
    seg('GND',74.0,80.3,75.5,80.3,.60), via('GND',75.5,80.3,.85,.40),

    # Modem bulk / HF decoupling: each power pad goes to the vertical B.Cu trunk.
    seg('LTE_3V8',38.7,84.0,39.5,84.0,.80), via('LTE_3V8',39.5,84.0,.85,.40),
    seg('LTE_3V8',38.7,90.0,39.5,90.0,.80), via('LTE_3V8',39.5,90.0,.85,.40),
    seg('LTE_3V8',38.8,74.2,39.5,74.2,.45), via('LTE_3V8',39.5,74.2,.70,.35),
    seg('LTE_3V8',38.8,76.4,39.5,76.4,.45),
    seg('LTE_3V8',38.8,78.6,39.5,78.6,.45), via('LTE_3V8',39.5,78.6,.70,.35),
    seg('LTE_3V8',34.8,72.0,35.5,72.0,.45), via('LTE_3V8',35.5,72.0,.70,.35),
    seg('LTE_3V8',35.5,72.0,39.5,74.2,.45,'B.Cu'),
    seg('GND',33.3,84.0,32.3,84.0,.60), via('GND',32.3,84.0,.85,.40),
    seg('GND',33.3,90.0,32.3,90.0,.60), via('GND',32.3,90.0,.85,.40),
    seg('GND',37.2,74.2,36.5,74.2,.25), via('GND',36.5,74.2,.60,.30),
    seg('GND',37.2,76.4,36.5,76.4,.25), via('GND',36.5,76.4,.60,.30),
    seg('GND',37.2,78.6,36.5,78.6,.25), via('GND',36.5,78.6,.60,.30),
    seg('GND',33.2,72.0,32.5,72.0,.25), via('GND',32.5,72.0,.60,.30),

    # U11 top row escapes vertically outward (toward decreasing Y).
    # GND pad 7.
    seg('GND',p_gnd[0],p_gnd[1],p_gnd[0],68.8,.55),
    via('GND',p_gnd[0],68.8,.80,.40),

    # COMP pad 6 -> compensation network.
    seg('LTE_COMP',p_comp[0],p_comp[1],p_comp[0],66.0,.25),
    seg('LTE_COMP',p_comp[0],66.0,83.2,65.8,.25),
    seg('LTE_COMP',83.2,65.8,83.2,67.8,.25),
    seg('LTE_COMP_RC',84.8,65.8,87.2,65.8,.25),
    seg('GND',88.8,65.8,89.5,65.8,.25), via('GND',89.5,65.8,.60,.30),
    seg('GND',84.8,67.8,85.5,67.8,.25), via('GND',85.5,67.8,.60,.30),

    # FB pad 5 -> divider. R61 is reversed so the FB pad faces U11.
    seg('LTE_FB',p_fb[0],p_fb[1],p_fb[0],69.5,.25),
    seg('LTE_FB',p_fb[0],69.5,83.2,69.5,.25),
    seg('LTE_FB',83.2,69.5,83.2,72.0,.25),
    seg('LTE_3V8',84.8,69.5,87.0,69.5,.25),
    via('LTE_3V8',87.0,69.5,.70,.35),
    seg('LTE_3V8',87.0,69.5,87.0,73.5,.30,'B.Cu'),
    seg('LTE_3V8',87.0,73.5,90.0,73.5,.30,'B.Cu'),
    seg('LTE_3V8',90.0,73.5,90.0,77.0,.30,'B.Cu'),
    seg('LTE_3V8',90.0,77.0,65.5,77.0,.30,'B.Cu'),
    seg('LTE_3V8',65.5,77.0,65.5,76.0,.30,'B.Cu'),
    seg('GND',84.8,72.0,85.5,72.0,.25), via('GND',85.5,72.0,.60,.30),

    # U11 bottom row escapes vertically outward (toward increasing Y).
    # RT goes to a clean right-side programming resistor.
    seg('LTE_RT',p_rt[0],p_rt[1],p_rt[0],79.5,.25),
    seg('LTE_RT',p_rt[0],79.5,91.2,79.5,.25),
    seg('LTE_RT',91.2,79.5,91.2,82.0,.25),
    seg('GND',92.8,82.0,95.0,82.0,.25), via('GND',95.0,82.0,.60,.30),

    # EN goes straight down to the local UVLO divider.
    seg('LTE_EN',p_en[0],p_en[1],p_en[0],88.0,.25),
    seg('LTE_EN',p_en[0],88.0,84.2,88.0,.25),
    seg('LTE_EN',82.0,88.0,80.2,90.0,.25),
    seg('GND',81.8,90.0,82.6,90.0,.25), via('GND',82.6,90.0,.60,.30),

    # U11 exposed pad ground.
    seg('GND',p_ep[0],p_ep[1],80.8,75.0,.80),
    via('GND',80.8,75.0,.90,.45),

    # Input capacitor grounds.
    seg('GND',82.0,81.3,82.0,80.5,.60), via('GND',82.0,80.5,.85,.40),
    seg('GND',88.0,81.3,88.0,80.5,.60), via('GND',88.0,80.5,.85,.40),
]
close=s.rfind(')'); s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

# Rely on existing extended GND pours for the small-signal and modem-cap GND pads.
# Postconditions.
for ref in ('L50','D50','C60','C61','C62','C63','C64','R60','R61','R62','R63','C65','C66','R64','R65','C67','C68','C69','C70','C71','C72'):
    if f'reference "{ref}"' not in s:
        raise RuntimeError(f'missing LTE power part {ref}')
_,_,u11c=find_fp(s,'U11')
for n in ('LTE_SW','LTE_BOOT','LTE_FB','LTE_COMP','LTE_EN','VIN_PROT','GND'):
    if n not in u11c: raise RuntimeError(f'U11 missing {n}')

P.write_text(s,encoding='utf-8')
print('Applied Rev.B LTE 3.8V power stage and A7683E VBAT bulk')
