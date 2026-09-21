from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
LIB=Path(__file__).with_name('revb_jlc.pretty')
s=P.read_text(encoding='utf-8')

# Rev.B low-cost vehicle connector:
# J4 = Mini-Fit/MX 4.2mm 8-way vertical 9A header, JLC C22365702.
# Mating harness side: common 8-way 2x4 Mini-Fit-compatible housing + female contacts.
# Board remains 118x95.
# Pinout chosen to reuse the Run-294 proven vehicle-routing corridors:
# 1 CAN1_H, 2 CAN1_L, 3 ACC_RAW, 4 J1708_A,
# 5 GND, 6 J1708_B, 7 BATT24_FUSED, 8 RESERVED/NC.

def balanced_end(text,start):
    depth=0; q=False; esc=False
    for j in range(start,len(text)):
        c=text[j]
        if q:
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=='"': q=False
            continue
        if c=='"': q=True
        elif c=='(': depth+=1
        elif c==')':
            depth-=1
            if depth==0: return j+1
    raise RuntimeError('unterminated s-expression')

def iter_blocks(text,token):
    i=0; needle='('+token
    while True:
        i=text.find(needle,i)
        if i<0:return
        j=balanced_end(text,i)
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
for n in ('BATT24_FUSED','GND','ACC_RAW','CAN1_H','CAN1_L','J1708_A','J1708_B'):
    if n not in net_id: raise RuntimeError(f'missing net {n}')

def ne(n,pad=False):
    return f'(net {net_id[n]} "{n}")' if pad else f'(net {net_id[n]})'

# Remove old generic J4 footprint only. Existing copper stubs are retained as
# intentional connection points and are continued to the new header below.
a,b,_=find_fp(s,'J4')
s=s[:a]+s[b:]

# Replace rectangular outline with 118 x 95 mm.
rem=[]
for a,b,blk in iter_blocks(s,'gr_line'):
    if '(layer "Edge.Cuts")' in blk or '(layer Edge.Cuts)' in blk:
        rem.append((a,b))
for a,b in reversed(rem):
    s=s[:a]+s[b:]
edge='''  (gr_line (start 0 0) (end 118 0) (stroke (width 0.2) (type solid)) (layer "Edge.Cuts"))
  (gr_line (start 118 0) (end 118 95) (stroke (width 0.2) (type solid)) (layer "Edge.Cuts"))
  (gr_line (start 118 95) (end 0 95) (stroke (width 0.2) (type solid)) (layer "Edge.Cuts"))
  (gr_line (start 0 95) (end 0 0) (stroke (width 0.2) (type solid)) (layer "Edge.Cuts"))'''
close=s.rfind(')')
s=s[:close]+'\n'+edge+'\n'+s[close:]

# Extend only GND polygons into the new connector wing. Leave the 3V3_MAIN
# In2 plane at x<=99.2 so the wing is quiet/ground referenced.
zones=[]
for a,b,blk in iter_blocks(s,'zone'):
    if ('(net_name "GND")' in blk or '(net "GND")' in blk) and '99.5' in blk:
        nb=blk.replace('(xy 99.5 0.5)','(xy 117.5 0.5)').replace('(xy 99.5 94.5)','(xy 117.5 94.5)')
        if nb!=blk: zones.append((a,b,nb))
for a,b,nb in reversed(zones):
    s=s[:a]+nb+s[b:]

# Keep descriptive board text truthful if present.
s=s.replace('100x95','118x95')

fn='CONN-TH_8P-P4.20_DLL-5566-8A.kicad_mod'
fp=(LIB/fn).read_text(encoding='utf-8')

# Drop external 3D model reference; manufacturing footprint remains exact.
for a,b,_ in reversed(list(iter_blocks(fp,'model'))):
    fp=fp[:a]+fp[b:]

# Convert old module syntax to embedded KiCad footprint.
eol=fp.find('\n')
if not fp.startswith('(module '): raise RuntimeError('unexpected vertical Mini-Fit footprint format')
fp='(footprint "RevB:CONN-TH_C4201WR-F-2X4P" (layer "F.Cu")\n\t(at 102.000 64.000 270.0)'+fp[eol:]
fp=re.sub(r'\(fp_text\s+reference\s+REF\*\*', '(fp_text reference "J4"', fp, count=1)
fp=re.sub(r'\(fp_text\s+value\s+[^\s\)]+', '(fp_text value "DLL-5566-8A C22365702"', fp, count=1)


padnets={'1':'CAN1_H','2':'CAN1_L','3':'ACC_RAW','4':'J1708_A',
         '5':'GND','6':'J1708_B','7':'BATT24_FUSED'}
repl=[]
for a,b,blk in iter_blocks(fp,'pad'):
    m=re.match(r'\(pad\s+"?([^"\s]+)"?',blk)
    if not m: continue
    pn=m.group(1)
    if pn not in padnets: continue
    n=padnets[pn]
    nb=blk[:-1]+' '+ne(n,True)
    if n=='GND': nb+=' (zone_connect 2)'
    nb+=')'
    repl.append((a,b,nb))
for a,b,nb in reversed(repl):
    fp=fp[:a]+nb+fp[b:]

close=s.rfind(')')
s=s[:close]+'\n'+fp+'\n'+s[close:]

# Exact coordinates for JLC footprint at (106.5,64), rotation 90 deg.
def board_xy(lx,ly,x=106.5,y=64.0,rot=90.0):
    a=math.radians(rot)
    return (x+lx*math.cos(a)+ly*math.sin(a),
            y-lx*math.sin(a)+ly*math.cos(a))

local={
 '7':(-6.30,-2.75),'8':(-6.30,2.75),'6':(-2.10,2.75),'4':(2.10,2.75),
 '2':(6.30,2.75),'1':(6.30,-2.75),'3':(2.10,-2.75),'5':(-2.10,-2.75)
}
pad={k:board_xy(*v) for k,v in local.items()}

def seg(n,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'
def via(n,x,y,size=.70,drill=.35):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

p1,p2,p3,p4,p5,p6,p7,p8=[pad[str(i)] for i in range(1,9)]
p_batt=pad['7']; p_acc=pad['3']; p_canh=pad['1']; p_canl=pad['2']; p_ja=pad['4']; p_jb=pad['6']
r=[
    # +24 V: proven F.Cu corridor.
    seg('BATT24_FUSED',97.80,53.00,100.40,53.00,1.20),
    seg('BATT24_FUSED',100.40,53.00,100.40,p_batt[1],1.20),
    seg('BATT24_FUSED',100.40,p_batt[1],p_batt[0],p_batt[1],1.20),

    # ACC: proven In2 corridor.
    via('ACC_RAW',97.80,45.50),
    seg('ACC_RAW',97.80,45.50,99.00,45.50,.30,'In2.Cu'),
    seg('ACC_RAW',99.00,45.50,99.00,62.50,.30,'In2.Cu'),
    seg('ACC_RAW',99.00,62.50,100.50,62.50,.30,'In2.Cu'),
    seg('ACC_RAW',100.50,62.50,100.50,p_acc[1],.30,'In2.Cu'),
    seg('ACC_RAW',100.50,p_acc[1],p_acc[0],p_acc[1],.30,'In2.Cu'),

    # J1939 H/L: proven independent In2 lanes.
    via('CAN1_H',94.20,43.00),
    seg('CAN1_H',94.20,43.00,94.20,41.50,.35,'In2.Cu'),
    seg('CAN1_H',94.20,41.50,102.00,41.50,.35,'In2.Cu'),
    seg('CAN1_H',102.00,41.50,102.00,p_canh[1],.35,'In2.Cu'),
    seg('CAN1_H',102.00,p_canh[1],p_canh[0],p_canh[1],.35,'In2.Cu'),

    via('CAN1_L',94.20,45.50),
    seg('CAN1_L',94.20,45.50,93.20,45.50,.35,'In2.Cu'),
    seg('CAN1_L',93.20,45.50,93.20,40.50,.35,'In2.Cu'),
    seg('CAN1_L',93.20,40.50,107.00,40.50,.35,'In2.Cu'),
    seg('CAN1_L',107.00,40.50,107.00,53.80,.35,'In2.Cu'),
    seg('CAN1_L',107.00,53.80,112.00,53.80,.35,'In2.Cu'),
    seg('CAN1_L',112.00,53.80,112.00,p_canl[1],.35,'In2.Cu'),
    seg('CAN1_L',112.00,p_canl[1],p_canl[0],p_canl[1],.35,'In2.Cu'),

    # J1708 A/B: proven F.Cu corridors from Run 294.
    seg('J1708_A',94.20,50.50,95.50,50.50,.35),
    seg('J1708_A',95.50,50.50,95.50,48.00,.35),
    seg('J1708_A',95.50,48.00,112.00,48.00,.35),
    seg('J1708_A',112.00,48.00,112.00,p_ja[1],.35),
    seg('J1708_A',112.00,p_ja[1],p_ja[0],p_ja[1],.35),

    seg('J1708_B',94.20,48.00,92.50,48.00,.35),
    seg('J1708_B',92.50,48.00,92.50,46.50,.35),
    seg('J1708_B',92.50,46.50,113.50,46.50,.35),
    seg('J1708_B',113.50,46.50,113.50,p_jb[1],.35),
    seg('J1708_B',113.50,p_jb[1],p_jb[0],p_jb[1],.35),
]
close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

# Strong postconditions.
_,_,j4=find_fp(s,'J4')
for n in ('BATT24_FUSED','GND','ACC_RAW','CAN1_H','CAN1_L','J1708_A','J1708_B'):
    if n not in j4: raise RuntimeError(f'J4 missing {n}')
if 'DLL-5566-8A C22365702' not in j4: raise RuntimeError('vertical Mini-Fit JLC footprint/value missing')
if 'Vehicle I/O 8-way' in j4: raise RuntimeError('generic J4 survived')
P.write_text(s,encoding='utf-8')
print(f'Applied low-cost vertical Mini-Fit J4 C22365702 at 106.5,64 rot90 on 118x95 board; pads={pad}')
