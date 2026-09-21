from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# D70's electrically clean location overlaps only the printed U9 reference.
# Hide that reference on F.SilkS; this does not affect the PCB reference,
# assembly BOM/CPL, pick-and-place, or footprint identity.
def balanced_block(text,start):
    depth=0; in_q=False; esc=False
    for j in range(start,len(text)):
        c=text[j]
        if in_q:
            if esc:
                esc=False
            elif c=='\\':
                esc=True
            elif c=='"':
                in_q=False
            continue
        if c=='"':
            in_q=True
        elif c=='(':
            depth+=1
        elif c==')':
            depth-=1
            if depth==0:
                return j+1
    raise RuntimeError('unterminated s-expression')

def move_u9_reference_to_fab(text):
    i=0
    while True:
        i=text.find('(footprint ',i)
        if i<0:
            raise RuntimeError('U9 footprint not found')
        j=balanced_block(text,i)
        blk=text[i:j]
        m=re.search(r'\(fp_text\s+reference\s+"?U9"?',blk)
        if m:
            t0=m.start()
            t1=balanced_block(blk,t0)
            txt=blk[t0:t1]
            txt2=re.sub(r'\(layer\s+"?F\.SilkS"?\)', '(layer "F.Fab")', txt, count=1)
            if txt2==txt:
                raise RuntimeError('U9 reference layer not recognized as F.SilkS')
            blk=blk[:t0]+txt2+blk[t1:]
            return text[:i]+blk+text[j:]
        i=j

s=move_u9_reference_to_fab(s)

# Rev.B A7683E PWRKEY ESD protection.
# D70 = Nexperia PESD5Z5.0,115 / LCSC C132368 / SOD-523.
# Official pinning: pin 1 = K (cathode) -> LTE_PWRKEY, pin 2 = A -> GND.
# Place immediately outside the modem edge, close to U9 pin 39.

net_pairs=[(int(i),n) for i,n in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)',s)]
if not net_pairs:
    raise RuntimeError('numeric net table missing')
net_id={n:i for i,n in net_pairs}
for n in ('LTE_PWRKEY','GND'):
    if n not in net_id:
        raise RuntimeError(f'missing net {n}')

def ne(n,pad=False):
    return f'(net {net_id[n]} "{n}")' if pad else f'(net {net_id[n]})'

# Compact SOD-523 footprint, oriented vertically.
# Body is ~1.2 x 0.8 mm; pads are kept outside the body.
d70=f'''  (footprint "RevB:SOD523_PWRKEY_ESD" (layer "F.Cu")
    (at 38.000 79.700 0)
    (attr smd)
    (fp_text reference "D70" (at -1.5 0 90) (layer "F.SilkS") hide (effects (font (size .7 .7) (thickness .1))))
    (fp_text value "PESD5Z5.0,115 C132368" (at 1.5 0 90) (layer "F.Fab") (effects (font (size .55 .55) (thickness .08))))
    (pad "1" smd roundrect (at 0.700 0) (size .80 .70) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .15) {ne('LTE_PWRKEY',True)})
    (pad "2" smd roundrect (at -0.700 0) (size .80 .70) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .15) {ne('GND',True)} (zone_connect 2))
  )'''

close=s.rfind(')')
s=s[:close]+'\n'+d70+'\n'+s[close:]

def seg(n,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'

def via(n,x,y,size=.60,drill=.30):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

r=[
    # Cathode branches from the existing PWRKEY path between U9 pin 39 and R71.
    # D70 sits left of the LTE_3V8 via/track cluster.
    seg('LTE_PWRKEY',40.300,81.100,39.500,80.500,.20),
    seg('LTE_PWRKEY',39.500,80.500,38.700,79.700,.20),

    # Anode gets a dedicated short ground return to the left.
    seg('GND',37.300,79.700,36.400,79.700,.25),
    via('GND',36.400,79.700,.65,.32),
]

close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

if 'reference "D70"' not in s:
    raise RuntimeError('D70 missing')
P.write_text(s,encoding='utf-8')
print('Applied Rev.B PWRKEY TVS D70 PESD5Z5.0 C132368')
