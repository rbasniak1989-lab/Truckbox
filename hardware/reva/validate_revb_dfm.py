from pathlib import Path
import csv,re

ROOT=Path(__file__).parent
BUILD=ROOT/'build'
s=(ROOT/'TruckBox_RevA.kicad_pcb').read_text(encoding='utf-8')

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

def fp(ref):
    for marker in (f'(property "Reference" "{ref}"',f'(fp_text reference "{ref}"'):
        m=s.find(marker)
        if m>=0:
            a=s.rfind('(footprint ',0,m); return s[a:balanced(s,a)]
    raise RuntimeError(f'missing {ref}')

def at(block):
    m=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)(?:\s+([-+0-9.]+))?\)',block)
    return float(m.group(1)),float(m.group(2)),float(m.group(3) or 0)

for ref in ('C1','C2'):
    b=fp(ref)
    assert 'TruckBox:1206_DFM' in b, f'{ref}: 1206 DFM footprint missing'
    sizes=[tuple(map(float,m)) for m in re.findall(r'\\(size\\s+([-+0-9.]+)\\s+([-+0-9.]+)\\)',b)]\n    n=sum(abs(x-1.15)<1e-3 and abs(y-1.90)<1e-3 for x,y in sizes)\n    assert n==2, f'{ref}: 1206 pad geometry mismatch sizes={sizes}'

qx,qy,_=at(fp('Q50')); rx,ry,_=at(fp('R70'))
dx=abs(qx-rx)
assert dx>=2.5, f'Q50/R70 spacing still unsafe: dx={dx:.3f}mm'

bom={}
with (BUILD/'TruckBox_RevB_JLC_BOM.csv').open(encoding='utf-8-sig',newline='') as f:
    for r in csv.DictReader(f):
        for ref in r['Designator'].split(','): bom[ref]=r
expected={'D3':'C7469913','D4':'C7469913','C1':'C7393990','C2':'C7393990',
          'C52':'C93190','C53':'C93190','C67':'C313070','C68':'C313070',
          'D1':'C2940165','J3':'C5184243','J5':'C7363812','L2':'C54534612'}
for ref,lcsc in expected.items():
    got=bom[ref]['JLCPCB Part #']
    assert got==lcsc, f'{ref}: expected {lcsc}, got {got}'

cpl={}
with (BUILD/'TruckBox_RevB_JLC_CPL.csv').open(encoding='utf-8-sig',newline='') as f:
    for r in csv.DictReader(f): cpl[r['Designator']]=r
for ref in ('C67','C68'):
    rot=float(cpl[ref]['Rotation'])%360
    assert abs(rot)<1e-6, f'{ref}: JLC polarity rotation should be 0, got {rot}'

print('dfm_gate=PASS')
print(f'Q50_R70_center_dx_mm={dx:.3f}')
print('C1_C2=1206/C7393990')
print('D3_D4=SC70-3/C7469913')
print('C67_C68=C313070/CPL_0deg')
