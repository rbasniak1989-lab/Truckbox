from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

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
    (at 40.000 79.000 0)
    (attr smd)
    (fp_text reference "D70" (at -1.5 0 90) (layer "F.SilkS") hide (effects (font (size .7 .7) (thickness .1))))
    (fp_text value "PESD5Z5.0,115 C132368" (at 1.5 0 90) (layer "F.Fab") (effects (font (size .55 .55) (thickness .08))))
    (pad "1" smd roundrect (at 0 0.700) (size .70 .80) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .15) {ne('LTE_PWRKEY',True)})
    (pad "2" smd roundrect (at 0 -0.700) (size .70 .80) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio .15) {ne('GND',True)} (zone_connect 2))
  )'''

close=s.rfind(')')
s=s[:close]+'\n'+d70+'\n'+s[close:]

def seg(n,x1,y1,x2,y2,w=.20,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") {ne(n)})'

def via(n,x,y,size=.60,drill=.30):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") {ne(n)})'

r=[
    # Cathode branches from the existing PWRKEY path between U9 pin 39 and R71.
    seg('LTE_PWRKEY',40.300,81.100,40.300,80.200,.20),
    seg('LTE_PWRKEY',40.300,80.200,40.000,79.700,.20),

    # Anode gets a dedicated short ground return.
    seg('GND',40.000,78.300,40.000,77.300,.25),
    via('GND',40.000,77.300,.65,.32),
]

close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(r)+'\n'+s[close:]

if 'reference "D70"' not in s:
    raise RuntimeError('D70 missing')
P.write_text(s,encoding='utf-8')
print('Applied Rev.B PWRKEY TVS D70 PESD5Z5.0 C132368')
