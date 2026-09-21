from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
LIB = Path(__file__).with_name('revb_jlc.pretty')
s = P.read_text(encoding='utf-8')

# Rev.B LTE physical / RF first pass.
# Uses the exact EasyEDA/LCSC footprints fetched by CI for the JLCPCB parts.
# Major component placement:
#   U9  A7683E C20617360 at (50,80), 90 deg
#   J5  nano-SIM C3033025 at (20,80), 90 deg
#   U10 TXU0202 C5186957 at (64,70)
#   U11 TPS54360B-Q1 C2687968 at (78,75)
#   J6  U.FL C88373 near ANT_MAIN at (37.5,69)
# Only the modem ground/VBAT and the complete RF chain are electrically
# introduced in this pass. Power, UART and SIM circuitry follow in later passes.

def balanced_block(text, start):
    depth = 0
    in_q = False
    esc = False
    for j in range(start, len(text)):
        c = text[j]
        if in_q:
            if esc:
                esc = False
            elif c == '\\':
                esc = True
            elif c == '"':
                in_q = False
            continue
        if c == '"':
            in_q = True
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return j + 1
    raise RuntimeError('unterminated s-expression')

def iter_blocks(text, token):
    i = 0
    needle = '(' + token
    while True:
        i = text.find(needle, i)
        if i < 0:
            return
        j = balanced_block(text, i)
        yield i, j, text[i:j]
        i = j

# --- Add Rev.B LTE nets ---
net_pairs = [(int(i), n) for i,n in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', s)]
if not net_pairs:
    raise RuntimeError('expected pre-DRC numeric net table')
net_id = {n:i for i,n in net_pairs}
new_nets = [
    'LTE_3V8',
    'LTE_ANT_MOD',
    'LTE_ANT_CONN',
]
next_id = max(net_id.values()) + 1
adds = []
for name in new_nets:
    if name not in net_id:
        net_id[name] = next_id
        adds.append(f'  (net {next_id} "{name}")')
        next_id += 1
if adds:
    matches=list(re.finditer(r'^  \(net \d+ "[^"]+"\)$',s,re.M))
    if not matches:
        raise RuntimeError('net insertion point not found')
    pos=matches[-1].end()
    s=s[:pos]+'\n'+'\n'.join(adds)+s[pos:]

def netexpr(name, pad=False):
    if pad:
        return f'(net {net_id[name]} "{name}")'
    return f'(net {net_id[name]})'

# --- Extend GND pours into the new 30 mm LTE bay ---
# Keep the 3V3_MAIN In2 plane deliberately restricted to the original core.
zone_repls=[]
for a,b,blk in iter_blocks(s,'zone'):
    is_gnd = ('(net_name "GND")' in blk) or (f'(net {net_id["GND"]})' in blk and 'GND' in blk)
    ml=re.search(r'\(layer\s+"?([^"\)]+)"?\)',blk)
    layer=ml.group(1) if ml else ''
    if is_gnd and layer in {'F.Cu','B.Cu','In1.Cu'}:
        nb=re.sub(r'\(xy\s+99\.5\s+64\.5\)', '(xy 99.5 94.5)', blk)
        nb=re.sub(r'\(xy\s+0\.5\s+64\.5\)', '(xy 0.5 94.5)', nb)
        zone_repls.append((a,b,nb))
for a,b,nb in reversed(zone_repls):
    s=s[:a]+nb+s[b:]

# --- Exact JLC footprint embedding ---
def read_fp(filename):
    p=LIB/filename
    if not p.exists():
        raise RuntimeError(f'JLC footprint missing: {p}')
    return p.read_text(encoding='utf-8')

def remove_blocks(text, token):
    ranges=[]
    for a,b,_ in iter_local_blocks(text,token):
        ranges.append((a,b))
    for a,b in reversed(ranges):
        text=text[:a]+text[b:]
    return text

def iter_local_blocks(text, token):
    i=0; needle='('+token
    while True:
        i=text.find(needle,i)
        if i<0:return
        j=balanced_block(text,i)
        yield i,j,text[i:j]
        i=j

def board_xy(lx, ly, x, y, rot):
    """Transform footprint-local XY to KiCad board XY."""
    import math
    a=math.radians(rot)
    return (
        x + lx*math.cos(a) + ly*math.sin(a),
        y - lx*math.sin(a) + ly*math.cos(a),
    )

def footprint_pad_xy(filename, padnum, x, y, rot):
    """Return board-space pad center using the exact fetched JLC footprint."""
    fp=read_fp(filename)
    target=None
    for _,_,blk in iter_local_blocks(fp,'pad'):
        pm=re.match(r'\(pad\s+"?([^"\s]+)"?',blk)
        if not pm or pm.group(1) != str(padnum):
            continue
        ma=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)(?:\s+[-+0-9.]+)?\)',blk)
        if not ma:
            raise RuntimeError(f'pad {padnum} has no at() in {filename}')
        target=tuple(map(float,ma.groups()))
        break
    if target is None:
        raise RuntimeError(f'pad {padnum} missing in {filename}')
    lx,ly=target
    # KiCad board coordinates use +Y downward on screen, therefore positive
    # footprint rotation transforms local coordinates clockwise in XY space.
    return board_xy(lx,ly,x,y,rot)

def embed_fp(filename, ref, value, x, y, rot, pad_nets=None, force_smd=False, npth_unnumbered=False):
    fp=read_fp(filename)
    # remove model references: geometry must be reproducible without external 3D assets
    ranges=[(a,b) for a,b,_ in iter_local_blocks(fp,'model')]
    for a,b in reversed(ranges):
        fp=fp[:a]+fp[b:]

    # easyeda2kicad can emit legacy fp_arc syntax:
    #   (fp_arc (start ...) (end ...) (angle ...))
    # KiCad 10 expects start/mid/end and refuses to parse the whole board.
    # These legacy arcs are footprint drawing primitives only; remove only the
    # incompatible legacy form while preserving pads, drills, nets and placement.
    legacy_arcs=[]
    for a,b,blk in iter_local_blocks(fp,'fp_arc'):
        if '(angle ' in blk and '(mid ' not in blk:
            legacy_arcs.append((a,b))
    for a,b in reversed(legacy_arcs):
        fp=fp[:a]+fp[b:]
    first=fp.find('(module ')
    if first != 0:
        raise RuntimeError(f'unexpected footprint format {filename}')
    eol=fp.find('\n')
    header=fp[:eol]
    m=re.match(r'\(module\s+([^\s]+)\s+\(layer\s+([^\)]+)\).*',header)
    if not m:
        raise RuntimeError(f'cannot parse module header {filename}: {header}')
    name=m.group(1).split(':')[-1]
    fp=f'(footprint "RevB:{name}" (layer "F.Cu")\n\t(at {x:.3f} {y:.3f} {rot:.1f})'+fp[eol:]
    fp=re.sub(r'\(fp_text\s+reference\s+REF\*\*', f'(fp_text reference "{ref}"', fp, count=1)
    fp=re.sub(r'\(fp_text\s+value\s+[^\s\)]+', f'(fp_text value "{value}"', fp, count=1)
    if force_smd:
        fp=fp.replace('(attr through_hole)','(attr smd)')
    if npth_unnumbered:
        mech=[]
        for a,b,blk in iter_local_blocks(fp,'pad'):
            if re.match(r'\(pad\s+""\s+thru_hole\b',blk):
                mech.append((a,b,blk.replace('(pad "" thru_hole','(pad "" np_thru_hole',1)))
        for a,b,nb in reversed(mech):
            fp=fp[:a]+nb+fp[b:]
    pad_nets=pad_nets or {}
    # Add board net to selected numbered pads.
    replacements=[]
    for a,b,blk in iter_local_blocks(fp,'pad'):
        pm=re.match(r'\(pad\s+"?([^"\s]+)"?',blk)
        if not pm:
            continue
        pn=pm.group(1)
        if pn in pad_nets:
            extra=' '+netexpr(pad_nets[pn],pad=True)
            if pad_nets[pn]=='GND':
                extra+=' (zone_connect 2)'
            nb=blk[:-1]+extra+')'
            replacements.append((a,b,nb))
    for a,b,nb in reversed(replacements):
        fp=fp[:a]+nb+fp[b:]
    return fp

modem_gnd = {str(n):'GND' for n in [8,13,19,21,27,30,31,33,36,37,45,63,66,67,69,70,71,72,73,74,75,76,77]}
modem_nets = dict(modem_gnd)
modem_nets.update({'34':'LTE_3V8','35':'LTE_3V8','32':'LTE_ANT_MOD'})

u9_file='COMM-SMD_L17.6-W15.7_A7683E.kicad_mod'
p32=footprint_pad_xy(u9_file,32,50,80,90)  # ANT_MAIN
p34=footprint_pad_xy(u9_file,34,50,80,90)  # VBAT
p35=footprint_pad_xy(u9_file,35,50,80,90)  # VBAT
print(f'U9 exact pads: 32={p32}, 34={p34}, 35={p35}')

# RF components sit immediately outside the ANT_MAIN corner so the 50-ohm
# fanout never crosses underneath the modem.
r50_pos=(43.00,70.80,180)
c50_pos=(44.00,69.00,90)
c51_pos=(40.50,69.00,90)
j6_pos=(37.50,69.00,0)

fps=[
    embed_fp('COMM-SMD_L17.6-W15.7_A7683E.kicad_mod','U9','A7683E C20617360',50,80,90,modem_nets),
    embed_fp('SIM-SMD_SIM8051-6-0-14-01-A.kicad_mod','J5','Nano-SIM C3033025',20,80,90,{},npth_unnumbered=True),
    embed_fp('VSSOP-8_L2.3-W2.0-P0.50-LS3.1-BR.kicad_mod','U10','TXU0202DCUR C5186957',64,70,0,{},True),
    embed_fp('SO-8_L4.9-W3.9-P1.27-LS6.0-BL-EP.kicad_mod','U11','TPS54360BQDDARQ1 C2687968',78,75,0,{}),
    embed_fp('ANT-SMD_UFL-R-SMT-1-10.kicad_mod','J6','U.FL-R-SMT-1(10) C88373',j6_pos[0],j6_pos[1],j6_pos[2],{'1':'GND','2':'LTE_ANT_CONN','3':'GND'}),
]

# Generic 0603 for the LTE pi match. C50/C51 are DNP tuning positions.
def fp0603(ref,val,x,y,rot,n1,n2):
    return f'''  (footprint "RevB:0603_RF" (layer "F.Cu")
    (at {x:.3f} {y:.3f} {rot})
    (attr smd)
    (fp_text reference "{ref}" (at 0 -1.6 {rot}) (layer "F.SilkS") hide (effects (font (size 0.8 0.8) (thickness 0.1))))
    (fp_text value "{val}" (at 0 1.4 {rot}) (layer "F.Fab") (effects (font (size 0.6 0.6) (thickness 0.1))))
    (pad "1" smd roundrect (at -0.5 0 {rot}) (size 0.65 0.9) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) {netexpr(n1,True)}{(' (zone_connect 2)' if n1=='GND' else '')})
    (pad "2" smd roundrect (at 0.5 0 {rot}) (size 0.65 0.9) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.2) {netexpr(n2,True)}{(' (zone_connect 2)' if n2=='GND' else '')})
  )'''

fps += [
    fp0603('R50','0R RF MATCH',r50_pos[0],r50_pos[1],r50_pos[2],'LTE_ANT_MOD','LTE_ANT_CONN'),
    fp0603('C50','DNP RF SHUNT',c50_pos[0],c50_pos[1],c50_pos[2],'LTE_ANT_MOD','GND'),
    fp0603('C51','DNP RF SHUNT',c51_pos[0],c51_pos[1],c51_pos[2],'LTE_ANT_CONN','GND'),
]

# Add two bottom mounting holes to support the longer 100x95 board.
def mount(ref,x,y):
    return f'''  (footprint "RevB:M3_NPTH" (layer "F.Cu")
    (at {x:.3f} {y:.3f})
    (attr through_hole)
    (fp_text reference "{ref}" (at 0 -4) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))
    (fp_text value "M3" (at 0 4) (layer "F.Fab") (effects (font (size 0.7 0.7) (thickness 0.1))))
    (pad "" np_thru_hole circle (at 0 0) (size 3.2 3.2) (drill 3.2) (layers "*.Cu" "*.Mask"))
  )'''
fps += [mount('H5',4,91),mount('H6',96,91)]

# Append footprints before board closing paren.
close=s.rfind(')')
if close<0: raise RuntimeError('board closing paren not found')
s=s[:close]+'\n'+'\n'.join(fps)+'\n'+s[close:]

# --- RF + local VBAT connectivity ---
def seg(name,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") {netexpr(name)})')

# Exact pad centres for our local RF footprints.
r50_1=board_xy(-0.5,0,r50_pos[0],r50_pos[1],r50_pos[2])
r50_2=board_xy( 0.5,0,r50_pos[0],r50_pos[1],r50_pos[2])
c50_1=board_xy(-0.5,0,c50_pos[0],c50_pos[1],c50_pos[2])
c51_1=board_xy(-0.5,0,c51_pos[0],c51_pos[1],c51_pos[2])
j6_rf=footprint_pad_xy('ANT-SMD_UFL-R-SMT-1-10.kicad_mod',2,j6_pos[0],j6_pos[1],j6_pos[2])

routes=[
    # Tie the two modem VBAT pins together locally. The regulator connection is
    # added by the dedicated LTE power pass.
    seg('LTE_3V8',p34[0],p34[1],p35[0],p35[1],.80),

    # ANT_MAIN first escapes straight outward between pad 32 and adjacent
    # GND pad 33, then widens after clearing the module edge.
    seg('LTE_ANT_MOD',p32[0],p32[1],p32[0],71.40,.22),
    seg('LTE_ANT_MOD',p32[0],71.40,r50_1[0],r50_1[1],.38),
    seg('LTE_ANT_MOD',r50_1[0],r50_1[1],c50_1[0],c50_1[1],.22),

    # Connector side of the pi network and U.FL.
    seg('LTE_ANT_CONN',r50_2[0],r50_2[1],c51_1[0],c51_1[1],.22),
    seg('LTE_ANT_CONN',c51_1[0],c51_1[1],j6_rf[0],j6_rf[1],.38),
]
close=s.rfind(')')
s=s[:close]+'\n'+'\n'.join(routes)+'\n'+s[close:]

# Mark LTE bay boundary on silkscreen only.
close=s.rfind(')')
mark='''  (gr_text "LTE / SIM / RF" (at 50 93) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))'''
s=s[:close]+'\n'+mark+'\n'+s[close:]

# Postconditions
for name in ('U9','J5','U10','U11','J6','R50','C50','C51','H5','H6'):
    if f'reference "{name}"' not in s:
        raise RuntimeError(f'missing {name}')
if 'C20617360' not in s or 'C2687968' not in s or 'C5186957' not in s:
    raise RuntimeError('JLC critical part identity missing')
if '(xy 99.5 94.5)' not in s or '(xy 0.5 94.5)' not in s:
    raise RuntimeError('LTE GND bay extension missing')

P.write_text(s,encoding='utf-8')
print(f'Applied Rev.B LTE exact-JLC placement + RF pass to {P}')
