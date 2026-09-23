from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.B.1 assembly-DFM correction after first JLC engineering review.
# C1/C2: 1210 bodies met electrical DRC but had near-zero corner clearance.
# 1206 keeps the same 3.2-mm body length and electrical value while narrowing
# the package. Centres and routed net endpoints remain frozen.
# Also normalize selected-part metadata so the generated PCB, position file and
# JLC BOM cannot silently carry stale Rev.A / pre-DFM part identifiers.

def balanced(text,start):
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
            if depth==0:return j+1
    raise RuntimeError('unterminated footprint')

def find_fp(text,ref):
    for marker in (f'(property "Reference" "{ref}"',f'(fp_text reference "{ref}"'):
        m=text.find(marker)
        if m>=0:
            a=text.rfind('(footprint ',0,m)
            if a<0: raise RuntimeError(f'footprint start missing {ref}')
            b=balanced(text,a)
            return a,b,text[a:b]
    raise RuntimeError(f'footprint {ref} missing')

def set_value(block, value):
    if '(property "Value"' in block:
        block,n=re.subn(r'(\(property\s+"Value"\s+")[^"]+("\s*)',
                        lambda m:m.group(1)+value+m.group(2),block,count=1)
    else:
        block,n=re.subn(r'(\(fp_text\s+value\s+")[^"]+("\s*)',
                        lambda m:m.group(1)+value+m.group(2),block,count=1)
    if n!=1:
        raise RuntimeError(f'could not set Value metadata to {value}')
    return block

def convert(block,ref):
    if '(footprint "TruckBox:1210"' not in block:
        raise RuntimeError(f'{ref}: expected TruckBox:1210 footprint')
    block=block.replace('(footprint "TruckBox:1210"','(footprint "TruckBox:1206_DFM"',1)
    if block.count('(size 1.150 2.700)')!=2:
        raise RuntimeError(f'{ref}: unexpected 1210 pad geometry')
    block=block.replace('(size 1.150 2.700)','(size 1.150 1.900)')
    old='(fp_rect (start -2.300 -1.600) (end 2.300 1.600)'
    if old not in block:
        raise RuntimeError(f'{ref}: 1210 courtyard missing')
    block=block.replace(old,'(fp_rect (start -2.100 -1.150) (end 2.100 1.150)',1)
    return block

for ref in ('C1','C2'):
    a,b,blk=find_fp(s,ref)
    s=s[:a]+convert(blk,ref)+s[b:]

selected_metadata={
    'D1':'SM8S33A C2940165',
    'D3':'ESD2CAN24DCKRQ1 C7469913',
    'D4':'ESD2CAN24DCKRQ1 C7469913',
    'J3':'USB4105-GF-A-120 C5184243',
    'J5':'SIM8051-6-0-14-00-A C7363812',
    'L2':'AHWC1608J47ND 47nH C54534612',
    'C1':'TCC1206X7R225K101HT 2.2uF 100V C7393990',
    'C2':'TCC1206X7R225K101HT 2.2uF 100V C7393990',
    'C52':'0603B222K500CT 2.2nF 50V C93190',
    'C53':'0603B222K500CT 2.2nF 50V C93190',
    'C67':'TPSD107K010R0050 100uF 10V C313070',
    'C68':'TPSD107K010R0050 100uF 10V C313070',
}
for ref,value in selected_metadata.items():
    a,b,blk=find_fp(s,ref)
    s=s[:a]+set_value(blk,value)+s[b:]

P.write_text(s,encoding='utf-8')
print('Applied Rev.B.1 DFM correction and normalized selected-part PCB metadata')
