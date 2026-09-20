from pathlib import Path
import re, math

LIB=Path(__file__).with_name('revb_jlc.pretty')

def balanced(text,start):
    d=0; q=False; esc=False
    for i in range(start,len(text)):
        ch=text[i]
        if q:
            if esc: esc=False
            elif ch=='\\': esc=True
            elif ch=='"': q=False
            continue
        if ch=='"': q=True
        elif ch=='(': d+=1
        elif ch==')':
            d-=1
            if d==0:return i+1
    raise RuntimeError('unterminated block')

def blocks(text,token):
    i=0
    while True:
        i=text.find('('+token,i)
        if i<0:return
        j=balanced(text,i)
        yield text[i:j]
        i=j

def board_xy(lx,ly,x,y,rot):
    a=math.radians(rot)
    return (x+lx*math.cos(a)+ly*math.sin(a),
            y-lx*math.sin(a)+ly*math.cos(a))

fn=LIB/'SO-8_L4.9-W3.9-P1.27-LS6.0-BL-EP.kicad_mod'
text=fn.read_text(encoding='utf-8')
print('U11 TPS54360B-Q1 exact JLC pad centers at board placement (78,75,0):')
for b in blocks(text,'pad'):
    m=re.match(r'\(pad\s+"?([^"\s]+)"?',b)
    a=re.search(r'\(at\s+([-+0-9.]+)\s+([-+0-9.]+)',b)
    if m and a:
        p=m.group(1)
        lx,ly=map(float,a.groups())
        x,y=board_xy(lx,ly,78,75,0)
        print(f'  pad {p}: ({x:.3f},{y:.3f}) local=({lx:.3f},{ly:.3f})')
