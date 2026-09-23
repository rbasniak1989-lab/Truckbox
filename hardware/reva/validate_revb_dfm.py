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
            a=s.rfind('(footprint ',0,m)
            return s[a:balanced(s,a)]
    raise RuntimeError(f'missing {ref}')

def at(block):
    m=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)(?:\s+([-+0-9.]+))?\)',block)
    return float(m.group(1)),float(m.group(2)),float(m.group(3) or 0)

def value(block):
    m=re.search(r'\(property\s+"Value"\s+"([^"]*)"',block)
    if not m:
        m=re.search(r'\(fp_text\s+value\s+"([^"]*)"',block)
    if not m:
        raise RuntimeError('Value metadata missing')
    return m.group(1)

for ref in ('C1','C2'):
    b=fp(ref)
    assert 'TruckBox:1206_DFM' in b, f'{ref}: 1206 DFM footprint missing'
    sizes=[tuple(map(float,m)) for m in re.findall(r'\(size\s+([-+0-9.]+)\s+([-+0-9.]+)\)',b)]
    n=sum(abs(x-1.15)<1e-3 and abs(y-1.90)<1e-3 for x,y in sizes)
    assert n==2, f'{ref}: 1206 pad geometry mismatch sizes={sizes}'

qx,qy,_=at(fp('Q50'))
rx,ry,_=at(fp('R70'))
dx=abs(qx-rx)
assert dx>=2.5, f'Q50/R70 spacing still unsafe: dx={dx:.3f}mm'

bom={}
with (BUILD/'TruckBox_RevB_JLC_BOM.csv').open(encoding='utf-8-sig',newline='') as f:
    for r in csv.DictReader(f):
        for ref in r['Designator'].split(','):
            bom[ref]=r

expected={
    'D3':'C7469913','D4':'C7469913',
    'C1':'C7393990','C2':'C7393990',
    'C52':'C93190','C53':'C93190',
    'C67':'C313070','C68':'C313070',
    'D1':'C2940165','J3':'C3025063',
    'J5':'C7363812','L2':'C54534612'
}
for ref,lcsc in expected.items():
    got=bom[ref]['JLCPCB Part #']
    assert got==lcsc, f'{ref}: expected {lcsc}, got {got}'

expected_board_values={
    'D1':'SM8S33A C2940165',
    'D3':'ESD2CAN24DCKRQ1 C7469913',
    'D4':'ESD2CAN24DCKRQ1 C7469913',
    'J3':'USB4105-GF-A-060 C3025063',
    'J5':'SIM8051-6-0-14-00-A C7363812',
    'L2':'AHWC1608J47ND 47nH C54534612',
    'C1':'TCC1206X7R225K101HT 2.2uF 100V C7393990',
    'C2':'TCC1206X7R225K101HT 2.2uF 100V C7393990',
    'C52':'0603B222K500CT 2.2nF 50V C93190',
    'C53':'0603B222K500CT 2.2nF 50V C93190',
    'C67':'TPSD107K010R0050 100uF 10V C313070',
    'C68':'TPSD107K010R0050 100uF 10V C313070',
}
for ref,expected_value in expected_board_values.items():
    got=value(fp(ref))
    assert got==expected_value, f'{ref}: stale PCB Value metadata: {got!r} != {expected_value!r}'

cpl={}
with (BUILD/'TruckBox_RevB_JLC_CPL.csv').open(encoding='utf-8-sig',newline='') as f:
    for r in csv.DictReader(f):
        cpl[r['Designator']]=r

for ref in ('C67','C68'):
    rot=float(cpl[ref]['Rotation'])%360
    assert abs(rot)<1e-6, f'{ref}: JLC polarity rotation should be 0, got {rot}'

print('dfm_gate=PASS')
print(f'Q50_R70_center_dx_mm={dx:.3f}')
print('C1_C2=1206/C7393990')
print('D3_D4=SC70-3/C7469913')
print('C67_C68=C313070/CPL_0deg')
print('pcb_metadata_gate=PASS')
