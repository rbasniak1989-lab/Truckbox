from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# ---------- S-expression footprint helpers ---------------------------------
def blocks(text, token='  (footprint '):
    out=[]; i=0
    while True:
        a=text.find(token,i)
        if a<0: break
        depth=0; ins=False; esc=False; b=None
        for j in range(a,len(text)):
            c=text[j]
            if ins:
                if esc: esc=False
                elif c=='\\': esc=True
                elif c=='"': ins=False
            else:
                if c=='"': ins=True
                elif c=='(': depth+=1
                elif c==')':
                    depth-=1
                    if depth==0: b=j+1; break
        if b is None: raise RuntimeError('unbalanced footprint')
        out.append((a,b,text[a:b])); i=b
    return out

def ref_of(blk):
    m=re.search(r'\(fp_text reference "([^"]+)"',blk)
    return m.group(1) if m else None

def edit_ref(text,ref,fn):
    for a,b,blk in blocks(text):
        if ref_of(blk)==ref: return text[:a]+fn(blk)+text[b:]
    raise RuntimeError(f'footprint {ref} not found')

def delete_ref(text,ref):
    for a,b,blk in blocks(text):
        if ref_of(blk)==ref: return text[:a]+text[b:]
    return text

def set_at(blk,x,y,rot=0):
    return re.sub(r'\n    \(at [^\n]+\)',f'\n    (at {x:.3f} {y:.3f} {rot})',blk,count=1)

def set_pad_local(blk,padno,x,y):
    pat=rf'(\(pad "{re.escape(str(padno))}" [^\n]*?\(at )[-0-9.]+ [-0-9.]+'
    out,n=re.subn(pat,rf'\g<1>{x:.3f} {y:.3f}',blk,count=1)
    if n!=1: raise RuntimeError(f'pad {padno} not found')
    return out

def swap_pad_nets(blk,p1,p2,id1,name1,id2,name2):
    lines=blk.splitlines()
    for i,line in enumerate(lines):
        if f'(pad "{p1}" ' in line:
            lines[i]=re.sub(r'\(net \d+ "[^"]+"\)',f'(net {id2} "{name2}")',line)
        elif f'(pad "{p2}" ' in line:
            lines[i]=re.sub(r'\(net \d+ "[^"]+"\)',f'(net {id1} "{name1}")',line)
    return '\n'.join(lines)

def quiet_silk(blk):
    # During DRC iterations hide footprint refs and remove generic body boxes.
    blk=re.sub(r'\n    \(fp_rect [^\n]*\(layer "F\.SilkS"\)\)', '', blk)
    blk=re.sub(r'(\(fp_text reference [^\n]*\(layer "F\.SilkS"\))',r'\1 hide',blk)
    return blk

def set_esp_geometry(blk,x):
    blk=set_at(blk,x,16.2,0)
    # Replace our centered generic courtyard with official Espressif extents.
    blk=re.sub(r'\(fp_rect \(start -9\.250 -13\.000\) \(end 9\.250 13\.000\)[^\n]*\(layer "F\.CrtYd"\)\)',
               '(fp_rect (start -9.800 -16.050) (end 9.800 10.550) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))',blk)
    return quiet_silk(blk)

# ---------- Mechanical / placement cleanup ---------------------------------
s=edit_ref(s,'U1',lambda b:set_esp_geometry(b,18))
s=edit_ref(s,'U2',lambda b:set_esp_geometry(b,82))

moves={
 'U5':(50,17,0),'J2':(39,13,0),'U7':(31,35.5,0),'U8':(22,36,0),'J1':(46,42,0),
 'U3':(72,42.5,0),'U4':(72,48.5,0),'D3':(87,42.5,0),'D4':(87,49.5,0),
 'D1':(80,59.5,0),'D2':(91,59.5,180),'U6':(59.5,59,180),'L1':(51,59,0),'D5':(58,54,0),
 'Q1':(67,7.5,0),'Q2':(55.5,33,0),'Q3':(88,37,0),'Q4':(22,31.5,0),'D7':(87,40,0),'SW1':(96,20.5,0),
 'C1':(56.5,54,0),'C2':(60.5,54,0),'R31':(64,63,0),'R32':(67,63,0),'R8':(61,63,0),'C5':(58,63,0),
 'C4':(61.5,55.5,0),'C7':(43.5,56.5,0),'C8':(43.5,60.5,0),'R6':(53,51.5,0),'R7':(53,53.5,0),
 'R9':(57,51,0),'C6':(60,51,0),
 'R13':(5.5,15,0),'C19':(5.5,18,0),'R29':(30,25,0),'R20':(30,21,0),'C9':(5.5,9,0),'C15':(5.5,12,0),
 'R33':(39,28,0),'R34':(43,28,0),
 'R3':(66.5,44,0),'R4':(66.5,50.5,0),'C11':(66.5,41,0),'C12':(66.5,47.5,0),
 'R24':(28,30,0),'R39':(31,30,0),'R40':(34,30,0),'C24':(18,36,0),'R5':(18,39,0),
 'R25':(56,42,0),'C18':(56,39,0),'R27':(55,30,0),
 'R10':(92,35,0),'R28':(92,37,0),'R11':(84,37,0),'R12':(84,34,0),'C21':(84,40,0),
 'R26':(64,9,0),'C10':(69,10.5,0),'C16':(69,13,0),'R14':(69,16,0),'C20':(69,19,0),
 'R42':(68,23,0),'R43':(68,25,0),'R30':(94,24.5,0),
 'C14':(59,13.5,0),'C17':(60,17,0),'R41':(59,20,0),'L2':(41,17,0),
}
for ref,(x,y,r) in moves.items():
    s=edit_ref(s,ref,lambda b,x=x,y=y,r=r:quiet_silk(set_at(b,x,y,r)))

# J4 keeps electrical numbering but physical rows are optimized for routing.
def fix_j4(blk):
    blk=set_at(blk,96,48,0)
    pos={4:(-1.8,-4.5),5:(1.8,-4.5),3:(-1.8,-1.5),8:(1.8,-1.5),
         6:(-1.8,1.5),7:(1.8,1.5),1:(-1.8,4.5),2:(1.8,4.5)}
    for p,(x,y) in pos.items(): blk=set_pad_local(blk,p,x,y)
    return quiet_silk(blk)
s=edit_ref(s,'J4',fix_j4)

# TVS channels are symmetric. Swap them so H/L stay uncrossed after the TVS.
s=edit_ref(s,'D3',lambda b:quiet_silk(swap_pad_nets(b,1,2,16,'CAN1_H',17,'CAN1_L')))
s=edit_ref(s,'D4',lambda b:quiet_silk(swap_pad_nets(b,1,2,20,'CAN2_H',21,'CAN2_L')))

# USB-C is deliberately omitted on Rev.A prototype. Native USB remains on service pads.
for ref in ['J3','D6','R1','R2']:
    s=delete_ref(s,ref)

# Replace provisional 1210 C3 with actual 8 mm Panasonic 47uF/63V footprint.
def c3_real(_old):
    return '''  (footprint "TruckBox:CP_Elec_D8x10.2" (layer "F.Cu")
    (at 67.5 59.5 0)
    (attr smd)
    (fp_text reference "C3" (at 0 -5.2) (layer "F.SilkS") hide (effects (font (size 0.8 0.8) (thickness 0.12))))
    (fp_text value "47uF/63V Panasonic EEETG1J470UP" (at 0 5.2) (layer "F.Fab") (effects (font (size 0.7 0.7) (thickness 0.1))))
    (fp_circle (center 0 0) (end 4 0) (stroke (width 0.1) (type solid)) (fill none) (layer "F.Fab"))
    (fp_rect (start -4.5 -4.5) (end 4.5 4.5) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))
    (pad "1" smd roundrect (at 3.1 0) (size 3.0 3.5) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15) (net 3 "VIN_PROT"))
    (pad "2" smd roundrect (at -3.1 0) (size 3.0 3.5) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.15) (net 1 "GND"))
  )'''
s=edit_ref(s,'C3',c3_real)

# Hide all remaining generic footprint silks to make DRC useful instead of noisy.
for ref in [ref_of(b) for _,_,b in blocks(s) if ref_of(b)]:
    s=edit_ref(s,ref,quiet_silk)

# ---------- Add mechanical and service footprints --------------------------
def testpad(ref,x,y,netid,netname):
    return f'''  (footprint "TruckBox:TP_1.5" (layer "F.Cu")
    (at {x} {y}) (attr smd)
    (fp_text reference "{ref}" (at 0 -1.8) (layer "F.SilkS") hide (effects (font (size 0.7 0.7) (thickness 0.1))))
    (fp_text value "{netname}" (at 0 1.8) (layer "F.Fab") (effects (font (size 0.6 0.6) (thickness 0.1))))
    (pad "1" smd circle (at 0 0) (size 1.5 1.5) (layers "F.Cu" "F.Mask") (net {netid} "{netname}"))
  )'''
def hole(ref,x,y):
    return f'''  (footprint "MountingHole:MountingHole_3.2mm_M3" (layer "F.Cu")
    (at {x} {y}) (attr exclude_from_pos_files exclude_from_bom)
    (fp_text reference "{ref}" (at 0 -4) (layer "F.SilkS") hide (effects (font (size 1 1) (thickness 0.15))))
    (fp_text value "M3" (at 0 4) (layer "F.Fab") hide (effects (font (size 1 1) (thickness 0.15))))
    (pad "" np_thru_hole circle (at 0 0) (size 3.2 3.2) (drill 3.2) (layers "*.Cu" "*.Mask"))
  )'''
extras=[
 testpad('TP_LINK_DM',64,23,56,'USB_D-_LINK'),testpad('TP_LINK_DP',64,25,57,'USB_D+_LINK'),
 hole('H1',4,4),hole('H2',96,4),hole('H3',4,61),hole('H4',96,61),
]

# ---------- Reroute only coordinate-verified critical nets -----------------
s='\n'.join(line for line in s.splitlines() if not line.startswith('  (segment ')) and not line.startswith('  (via '))+'\n'
def seg(netid,x1,y1,x2,y2,w=.30,layer='F.Cu'):
    return f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) (width {w:.3f}) (layer "{layer}") (net {netid}))'
def via(netid,x,y,size=.8,drill=.4):
    return f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) (layers "F.Cu" "B.Cu") (net {netid}))'

r=[]
# CAN1, U3/D3 centered 72/87 at y42.5.
r += [seg(16,94.2,43.5,90,41.85,.30),seg(16,90,41.85,86,41.85,.30),
      seg(17,97.8,43.5,91,43.15,.30),seg(17,91,43.15,86,43.15,.30),
      seg(16,86,41.85,74.7,41.865,.30),seg(17,86,43.15,74.7,43.135,.30)]
# CAN2, U4 y48.5 / D4 y49.5.
r += [seg(20,94.2,49.5,90,48.85,.30),seg(20,90,48.85,86,48.85,.30),
      seg(21,97.8,49.5,91,50.15,.30),seg(21,91,50.15,86,50.15,.30),
      seg(20,86,48.85,80,48.85,.30),seg(20,80,48.85,74.7,47.865,.30),
      seg(21,86,50.15,80,50.15,.30),seg(21,80,50.15,74.7,49.135,.30)]
# GNSS RF from U5 pin11 to U.FL center pad.
r += [seg(52,45.15,13.7,42.5,13.7,.40),seg(52,42.5,13.7,37.95,13.0,.40)]
# Power skeleton: J4 B+ -> reverse diode -> TVS/input bulk -> U6 VIN.
r += [seg(2,94.2,52.5,95.0,56.0,1.0),seg(2,95.0,56.0,93.35,59.5,1.0),
      seg(3,88.65,59.5,86.0,59.5,1.0),seg(3,86.0,59.5,70.6,59.5,1.0),
      seg(3,70.6,59.5,61.65,59.5,.8)]
# Buck PH -> inductor and catch diode, output rail stub.
r += [seg(7,57.35,60.0,55.5,60.0,.8),seg(7,55.5,60.0,54.0,59.0,.8),
      seg(7,57.35,60.0,58.0,57.0,.8),seg(7,58.0,57.0,60.4,54.0,.8),
      seg(9,48.0,59.0,46.0,59.0,.8)]
# High-current ground connections to the solid inner GND plane.
r += [seg(1,74.0,59.5,72.5,59.5,1.0),via(1,72.5,59.5,1.0,.5),
      via(1,72.5,58.5,1.0,.5),via(1,72.5,60.5,1.0,.5)]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(extras+r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied Rev.A v2 placement/routing fixes to {P}')
