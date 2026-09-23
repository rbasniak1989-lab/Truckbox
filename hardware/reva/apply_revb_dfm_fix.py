from pathlib import Path
import re

P=Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s=P.read_text(encoding='utf-8')

# Rev.B.1 assembly-DFM correction after first JLC engineering review.
# C1/C2: 1210 bodies met electrical DRC but had near-zero corner clearance.
# 1206 keeps the same 3.2-mm body length and electrical value while narrowing
# the package. Centres and routed net endpoints remain frozen.

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

P.write_text(s,encoding='utf-8')
print('Applied Rev.B.1 DFM footprint correction: C1/C2 1206, centres/routes frozen')
