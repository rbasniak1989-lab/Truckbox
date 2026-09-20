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

# Use U2 GPIO15 (module pad 23) to control LTE regulator enable.
a,b,u2=find_fp(s,'U2')
u2=assign_pad(u2,23,'LTE_EN')
s=s[:a]+u2+s[b:]

# Refresh coordinates after net edits.
_,_,u11=find_fp(s,'U11'); _,_,u9=find_fp(s,'U9'); _,_,d1=find_fp(s,'D1')
p_boot=pad_xy(u11,1); p_vin=pad_xy(u11,2); p_en=pad_xy(u11,3); p_rt=pad_xy(u11,4)
p_fb=pad_xy(u11,5); p_comp=pad_xy(u11,6); p_gnd=pad_xy(u11,7); p_sw=pad_xy(u11,8); p_ep=pad_xy(u11,9)
p34=pad_xy(u9,34); p35=pad_xy(u9,35)
# D1 VIN_PROT pad is pad 2 in Rev.A.
p_vinsrc=pad_xy(d1,2)

def fp2(ref,val,x,y,rot,n1,n2,kind='0603'):
    if kind=='0603': dx=.80; psx=.75; psy=.95
    elif kind=='1210': dx=1.70; psx=1.50; psy=2.60
    elif kind=='7343': dx=2.70; psx=2.50; psy=4.20
    elif kind=='SMB': dx=2.20; psx=2.10; psy=2.80
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
    return f'''  (footprint "RevB:L_7x7" (layer "F.Cu")
    (at {x:.3f} {y:.3f})
    (attr smd)
    (fp_text reference "{ref}" (at 0 -4.2) (layer "F.SilkS") hide (effects (font (size .8 .8) (thickness .1))))
    (fp_text value "{val}" (at 0 4.2) (layer "F.Fab") (effects (font (size .7 .7) (thickness .1))))
    (pad "1" smd roundrect (at -3.2 0) (size 2.4 5.8) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .12) {ne(n1,True)})
    (pad "2" smd roundrect (at 3.2 0) (size 2.4 5.8) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .12) {ne(n2,True)})
  )'''

parts=[
    # Power stage
    ind('L50','22uH >=4A',71.0,68.3,'LTE_3V8','LTE_SW'),
    fp2('D50','B560C 60V 5A',79.0,67.0,0,'LTE_SW','GND','SMB'),
    fp2('C60','100nF BOOT',73.3,76.0,90,'LTE_BOOT','LTE_SW','0603'),
    fp2('C61','2.2uF 50V VIN',84.0,80.5,0,'VIN_PROT','GND','1210'),
    fp2('C62','2.2uF 50V VIN',88.0,80.5,0,'VIN_PROT','GND','1210'),
    fp2('C63','47uF 10V OUT',70.0,83.5,0,'LTE_3V8','GND','1210'),
    fp2('C64','47uF 10V OUT',75.0,83.5,0,'LTE_3V8','GND','1210'),

    # 400-kHz programming, 3.8-V feedback and compensation.
    fp2('R60','243k RT 400kHz',84.0,85.5,0,'LTE_RT','GND','0603'),
    fp2('R61','38.3k FB HIGH',85.0,73.0,0,'LTE_3V8','LTE_FB','0603'),
    fp2('R62','10.2k FB LOW',85.0,75.5,0,'LTE_FB','GND','0603'),
    fp2('R63','6.34k COMP',85.0,68.5,0,'LTE_COMP','LTE_COMP_RC','0603'),
    fp2('C65','56nF COMP',88.0,68.5,0,'LTE_COMP_RC','GND','0603'),
    fp2('C66','100pF COMP POLE',85.0,70.5,0,'LTE_COMP','GND','0603'),
    fp2('R64','100k LTE EN PD',82.0,86.5,0,'LTE_EN','GND','0603'),

    # A7683E local VBAT reservoir / RF decoupling.
    fp2('C67','100uF 10V VBAT',36.0,82.0,90,'LTE_3V8','GND','7343'),
    fp2('C68','100uF 10V VBAT',36.0,87.0,90,'LTE_3V8','GND','7343'),
    fp2('C69','1uF VBAT',40.0,82.0,90,'LTE_3V8','GND','0603'),
    fp2('C70','100nF VBAT',40.0,84.2,90,'LTE_3V8','GND','0603'),
    fp2('C71','33pF VBAT',40.0,86.4,90,'LTE_3V8','GND','0603'),
    fp2('C72','10pF VBAT',40.0,88.6,90,'LTE_3V8','GND','0603'),
]
close=s.rfind(')'); s=s[:close]+'\n'+'\n'.join(parts)+'\n'+s[close:]

def seg(n,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'
def via(n,x,y,size=.75,drill=.35):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

# Component pad centers from the generic footprints above.
# L50: out=(67.8,68.3), SW=(74.2,68.3)
r=[
    # Protected 24-V input down the empty right side of LTE bay.
    seg('VIN_PROT',p_vinsrc[0],p_vinsrc[1],90.0,62.0,1.00,'F.Cu'),
    seg('VIN_PROT',90.0,62.0,90.0,78.0,1.00,'F.Cu'),
    seg('VIN_PROT',90.0,78.0,p_vin[0],p_vin[1],1.00,'F.Cu'),
    seg('VIN_PROT',84.0-1.7,80.5,90.0,78.0,.60),
    seg('VIN_PROT',88.0-1.7,80.5,90.0,78.0,.60),

    # SW, catch diode, inductor and bootstrap.
    seg('LTE_SW',p_sw[0],p_sw[1],74.2,68.3,.80),
    seg('LTE_SW',74.2,68.3,76.8,67.0,.80),
    seg('LTE_SW',74.1,75.2,p_sw[0],p_sw[1],.35),
    seg('LTE_BOOT',p_boot[0],p_boot[1],72.5,76.0,.30),

    # Regulator output and 2-mm modem feed around the bottom of the module.
    seg('LTE_3V8',67.8,68.3,67.0,82.7,1.20),
    seg('LTE_3V8',67.0,82.7,68.3,83.5,1.20),
    seg('LTE_3V8',68.3,83.5,73.3,83.5,1.20),
    seg('LTE_3V8',67.0,82.7,67.0,90.5,2.00),
    seg('LTE_3V8',67.0,90.5,39.0,90.5,2.00),
    seg('LTE_3V8',39.0,90.5,39.0,p34[1],2.00),
    seg('LTE_3V8',39.0,p34[1],p34[0],p34[1],2.00),
    seg('LTE_3V8',p34[0],p34[1],p35[0],p35[1],1.20),

    # Local A7683E bulk/decoupling connections to the 2-mm spine.
    seg('LTE_3V8',39.0,82.0,36.0-2.7,82.0,.80),
    seg('LTE_3V8',39.0,87.0,36.0-2.7,87.0,.80),
    seg('LTE_3V8',39.0,82.0,40.0,82.0,.50),
    seg('LTE_3V8',39.0,84.2,40.0,84.2,.50),
    seg('LTE_3V8',39.0,86.4,40.0,86.4,.50),
    seg('LTE_3V8',39.0,88.6,40.0,88.6,.50),

    # FB/RT/COMP/EN.
    seg('LTE_FB',p_fb[0],p_fb[1],84.2,73.0,.25),
    seg('LTE_FB',85.8,73.0,84.2,75.5,.25),
    seg('LTE_RT',p_rt[0],p_rt[1],83.2,85.5,.25),
    seg('LTE_COMP',p_comp[0],p_comp[1],84.2,68.5,.25),
    seg('LTE_COMP',p_comp[0],p_comp[1],84.2,70.5,.25),
    seg('LTE_COMP_RC',85.8,68.5,87.2,68.5,.25),
    seg('LTE_EN',p_en[0],p_en[1],81.2,86.5,.25),

    # Ground U11 pin + exposed pad with nearby stitches.
    seg('GND',p_gnd[0],p_gnd[1],77.4,70.2,.60),
    via('GND',77.4,70.2,.80,.40),
    seg('GND',p_ep[0],p_ep[1],80.0,75.0,.80),
    via('GND',80.0,75.0,.90,.45),

    # Ground returns for power stage/bulk parts, stitched locally.
    seg('GND',81.2,67.0,82.3,67.0,.60), via('GND',82.3,67.0,.80,.40),
    seg('GND',85.7,80.5,85.7,82.0,.60), via('GND',85.7,82.0,.80,.40),
    seg('GND',89.7,80.5,89.7,82.0,.60), via('GND',89.7,82.0,.80,.40),
    seg('GND',71.7,83.5,71.7,86.0,.80), via('GND',71.7,86.0,.90,.45),
    seg('GND',76.7,83.5,76.7,86.0,.80), via('GND',76.7,86.0,.90,.45),
]
close=s.rfind(')'); s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

# Rely on existing extended GND pours for the small-signal and modem-cap GND pads.
# Postconditions.
for ref in ('L50','D50','C60','C61','C62','C63','C64','R60','R61','R62','R63','C65','C66','R64','C67','C68','C69','C70','C71','C72'):
    if f'reference "{ref}"' not in s:
        raise RuntimeError(f'missing LTE power part {ref}')
_,_,u11c=find_fp(s,'U11')
for n in ('LTE_SW','LTE_BOOT','LTE_FB','LTE_COMP','LTE_EN','VIN_PROT','GND'):
    if n not in u11c: raise RuntimeError(f'U11 missing {n}')

P.write_text(s,encoding='utf-8')
print('Applied Rev.B LTE 3.8V power stage and A7683E VBAT bulk')
