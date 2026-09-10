from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', s)}
id_net = {i: name for name, i in net_id.items()}


def balanced_block(text, start):
    depth = 0; in_q = False; esc = False
    for j in range(start, len(text)):
        c = text[j]
        if in_q:
            if esc: esc = False
            elif c == '\\': esc = True
            elif c == '"': in_q = False
            continue
        if c == '"': in_q = True
        elif c == '(': depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0: return j + 1
    raise RuntimeError('unterminated s-expression')


def block_net(b):
    m = re.search(r'\(net(?:\s+\d+)?\s+"([^"]+)"\)', b)
    if m: return m.group(1)
    m = re.search(r'\(net\s+(\d+)\)', b)
    return id_net.get(int(m.group(1))) if m else None


def remove_blocks(text, token, pred):
    ranges=[]; pos=0; needle='('+token
    while True:
        i=text.find(needle,pos)
        if i<0: break
        j=balanced_block(text,i); b=text[i:j]
        if pred(b): ranges.append((i,j))
        pos=j
    for i,j in reversed(ranges): text=text[:i]+text[j:]
    return text


# Rebuild only the nets implicated by run-136 DRC. All other v4 geometry stays
# frozen. Removing by electrical net works with both numeric and named KiCad
# serialization.
for net in ('USB_D-_LINK','USB_D+_LINK','LINK_BOOT'):
    s=remove_blocks(s,'segment',lambda b,n=net:block_net(b)==n)
    s=remove_blocks(s,'via',lambda b,n=net:block_net(b)==n)


def seg(net,layer,x1,y1,x2,y2,w=.20):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f}) '
            f'(width {w:.3f}) (layer "{layer}") (net "{net}"))')


def via(net,x,y,size=.55,drill=.30):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) (drill {drill:.3f}) '
            f'(layers "F.Cu" "B.Cu") (net "{net}"))')

r=[]

# LINK_BOOT: leave the ESP32 pad horizontally, so the trace never converges on
# GNSS_PPS. The layer change is outside U2 and well left of J3's shell stake.
r += [
    seg('LINK_BOOT','F.Cu',90.75,24.45,91.80,24.45),
    via('LINK_BOOT',91.80,24.45),
    seg('LINK_BOOT','B.Cu',91.80,24.45,91.80,18.90),
    via('LINK_BOOT',91.80,18.90),
    seg('LINK_BOOT','F.Cu',91.80,18.90,94.50,18.90),
    seg('LINK_BOOT','F.Cu',94.50,18.90,94.50,20.50),
    seg('LINK_BOOT','F.Cu',94.50,20.50,94.50,16.50),
]

# D+ main route remains on F.Cu below U2. Its final via is also the feed point
# for the B.Cu same-net merge of the two reversible D+ contacts.
r += [
    seg('USB_D+_LINK','F.Cu',68.50,25.00,68.80,25.00),
    seg('USB_D+_LINK','F.Cu',68.80,25.00,68.80,31.40),
    seg('USB_D+_LINK','F.Cu',68.80,31.40,90.20,31.40),
    via('USB_D+_LINK',90.20,31.40),
]

# D- must cross the frozen MCU-side D+ route. Do that on B.Cu only after
# moving to x=72.8, then return to F.Cu at y=32.2. This avoids the run-136
# CORE_TO_LINK and CAN_MODE clearance hits at (72.8,30.6).
r += [
    seg('USB_D-_LINK','F.Cu',68.50,23.00,69.80,24.00),
    via('USB_D-_LINK',69.80,24.00),
    seg('USB_D-_LINK','B.Cu',69.80,24.00,72.80,24.00),
    seg('USB_D-_LINK','B.Cu',72.80,24.00,72.80,32.20),
    via('USB_D-_LINK',72.80,32.20),
    seg('USB_D-_LINK','F.Cu',72.80,32.20,91.00,32.20),
    via('USB_D-_LINK',91.00,32.20),
]

# USB-C contact fanout. First leave every 0.5-mm-pitch contact horizontally;
# only after x=91.5 do the four tracks spread to 0.9-mm vertical pitch. Thus
# the minimum different-net copper clearance is >=0.20 mm all the way out of
# the connector row. Standard 0.55/0.30 vias are used throughout.
r += [
    # Top D+
    seg('USB_D+_LINK','F.Cu',92.645,27.850,91.500,27.850),
    seg('USB_D+_LINK','F.Cu',91.500,27.850,90.200,27.400),
    via('USB_D+_LINK',90.200,27.400),
    # Upper D-
    seg('USB_D-_LINK','F.Cu',92.645,28.350,91.500,28.350),
    seg('USB_D-_LINK','F.Cu',91.500,28.350,91.000,28.300),
    via('USB_D-_LINK',91.000,28.300),
    # Lower D+
    seg('USB_D+_LINK','F.Cu',92.645,28.850,91.500,28.850),
    seg('USB_D+_LINK','F.Cu',91.500,28.850,90.200,29.200),
    via('USB_D+_LINK',90.200,29.200),
    # Bottom D-
    seg('USB_D-_LINK','F.Cu',92.645,29.350,91.500,29.350),
    seg('USB_D-_LINK','F.Cu',91.500,29.350,91.000,30.100),
    via('USB_D-_LINK',91.000,30.100),

    # Same-net merges on B.Cu, safely to the right of the UART lanes.
    seg('USB_D+_LINK','B.Cu',90.20,27.40,90.20,31.40),
    seg('USB_D-_LINK','B.Cu',91.00,28.30,91.00,32.20),
]

pos=s.rfind('\n)')
if pos<0: raise RuntimeError('final PCB paren not found')
s=s[:pos]+'\n'+'\n'.join(r)+s[pos:]
P.write_text(s,encoding='utf-8')
print(f'Applied USB-C fanout refinement v5 to {P}')
