from pathlib import Path
import re, math

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
LIB=Path(__file__).with_name('revb_jlc.pretty')
s=P.read_text(encoding='utf-8')

# Rev.B SIM ESD pass 1.
# D71: TECH PUBLIC SRV05-4, JLCPCB C558418, exact EasyEDA/JLC footprint.
# Standard SRV05-4 pinout: 1/3/4/6=I/O, 2=GND, 5=VCC.
# We use 1=DATA, 4=RST, 6=CLK, 5=SIM_VDD, 2=GND, 3=NC.

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

net_pairs=[(int(i),n) for i,n in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)',s)]
if not net_pairs: raise RuntimeError('numeric net table missing')
net_id={n:i for i,n in net_pairs}
for n in ('GND','SIM_VDD','SIM_RST_CARD','SIM_CLK_CARD','SIM_DATA_CARD'):
    if n not in net_id: raise RuntimeError(f'missing net {n}')

def ne(n,pad=False):
    return f'(net {net_id[n]} "{n}")' if pad else f'(net {net_id[n]})'

def read_fp(filename):
    p=LIB/filename
    if not p.exists(): raise RuntimeError(f'JLC footprint missing: {p}')
    return p.read_text(encoding='utf-8')

def board_xy(lx,ly,x,y,rot):
    a=math.radians(rot)
    return (x+lx*math.cos(a)+ly*math.sin(a),
            y-lx*math.sin(a)+ly*math.cos(a))

def footprint_pad_xy(filename,padnum,x,y,rot):
    fp=read_fp(filename)
    for _,_,blk in iter_blocks(fp,'pad'):
        pm=re.match(r'\(pad\s+"?([^"\s]+)"?',blk)
        if not pm or pm.group(1)!=str(padnum): continue
        ma=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)(?:\s+[-+0-9.]+)?\)',blk)
        if not ma: raise RuntimeError(f'pad {padnum} has no at()')
        return board_xy(*map(float,ma.groups()),x,y,rot)
    raise RuntimeError(f'pad {padnum} missing')

def embed_fp(filename,ref,value,x,y,rot,pad_nets):
    fp=read_fp(filename)
    # Strip external 3D models and legacy arcs for deterministic KiCad 10 parsing.
    ranges=[(a,b) for a,b,_ in iter_blocks(fp,'model')]
    for a,b in reversed(ranges): fp=fp[:a]+fp[b:]
    legacy=[(a,b) for a,b,blk in iter_blocks(fp,'fp_arc') if '(angle ' in blk and '(mid ' not in blk]
    for a,b in reversed(legacy): fp=fp[:a]+fp[b:]
    if not fp.startswith('(module '): raise RuntimeError('unexpected external footprint format')
    eol=fp.find('\n')
    header=fp[:eol]
    m=re.match(r'\(module\s+([^\s]+)\s+\(layer\s+([^\)]+)\).*',header)
    if not m: raise RuntimeError('cannot parse footprint header')
    name=m.group(1).split(':')[-1]
    fp=f'(footprint "RevB:{name}" (layer "F.Cu")\n\t(at {x:.3f} {y:.3f} {rot:.1f})'+fp[eol:]
    fp=re.sub(r'\(fp_text\s+reference\s+REF\*\*',f'(fp_text reference "{ref}"',fp,count=1)
    fp=re.sub(r'\(fp_text\s+value\s+[^\s\)]+',f'(fp_text value "{value}"',fp,count=1)
    repl=[]
    for a,b,blk in iter_blocks(fp,'pad'):
        pm=re.match(r'\(pad\s+"?([^"\s]+)"?',blk)
        if not pm: continue
        pn=pm.group(1)
        if pn not in pad_nets: continue
        n=pad_nets[pn]
        extra=' '+ne(n,True)
        if n=='GND': extra+=' (zone_connect 2)'
        repl.append((a,b,blk[:-1]+extra+')'))
    for a,b,nb in reversed(repl): fp=fp[:a]+nb+fp[b:]
    return fp

def seg(n,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'

def via(n,x,y,size=.60,drill=.30):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

fn='SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL.kicad_mod'
x,y,rot=12.5,80.0,0.0
pads={pn:footprint_pad_xy(fn,pn,x,y,rot) for pn in ('1','2','3','4','5','6')}

fp=embed_fp(fn,'D71','SRV05-4 C558418',x,y,rot,{
    '1':'SIM_DATA_CARD',
    '2':'GND',
    '4':'SIM_RST_CARD',
    '5':'SIM_VDD',
    '6':'SIM_CLK_CARD',
})

close=s.rfind(')')
s=s[:close]+'\n'+fp+'\n'+s[close:]

# Fanout D71 radially so no two protected nets share an escape corridor.
# Existing through-vias from the Run-279 baseline are used as the far endpoints.
p1,p2,p4,p5,p6=(pads[k] for k in ('1','2','4','5','6'))
routes=[
    # DATA (pad 1): down, then B.Cu to the approved DATA_CARD via at (18,82.54).
    seg('SIM_DATA_CARD',p1[0],p1[1],p1[0],82.30),
    via('SIM_DATA_CARD',p1[0],82.30),
    seg('SIM_DATA_CARD',p1[0],82.30,18.00,82.54,.20,'B.Cu'),

    # GND (pad 2): independent short return straight down into the planes.
    seg('GND',p2[0],p2[1],p2[0],82.40,.25),
    via('GND',p2[0],82.40,.70,.35),

    # CLK (pad 6): up, then B.Cu above the socket to the approved CLK via.
    seg('SIM_CLK_CARD',p6[0],p6[1],p6[0],77.70),
    via('SIM_CLK_CARD',p6[0],77.70),
    seg('SIM_CLK_CARD',p6[0],77.70,31.80,81.00,.20,'B.Cu'),

    # VDD reference (pad 5): up on its own via, then In1 to the proven VDD via.
    seg('SIM_VDD',p5[0],p5[1],p5[0],77.45),
    via('SIM_VDD',p5[0],77.45),
    seg('SIM_VDD',p5[0],77.45,32.00,76.60,.20,'In1.Cu'),

    # RST (pad 4): up, then In2 to the approved RST_CARD via at (27.5,80).
    seg('SIM_RST_CARD',p4[0],p4[1],p4[0],77.70),
    via('SIM_RST_CARD',p4[0],77.70),
    seg('SIM_RST_CARD',p4[0],77.70,27.50,80.00,.20,'In2.Cu'),
]
close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(routes)+'\n'+s[close:]

P.write_text(s,encoding='utf-8')
print(f'Applied Rev.B SIM ESD pass 1: D71 SRV05-4 C558418 at {(x,y,rot)} pads={pads}')
