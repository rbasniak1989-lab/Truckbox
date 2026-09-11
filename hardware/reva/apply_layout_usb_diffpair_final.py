from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Final USB2 Full-Speed routing refinement.
# Keep the proven Type-C/ESD/CC footprints, but replace only the four USB data
# nets. The long D+/D- run stays on F.Cu over the solid L2 GND plane. The
# mirrored Type-C duplicates use one short B.Cu bridge per signal (two vias per
# signal), reducing the old data-path layer transitions from 12 to 4 total.
# Series 22R resistors remain at the ESP32-C6 side.

USB_NETS = (
    'USB_D-_LINK_MCU', 'USB_D+_LINK_MCU',
    'USB_D-_LINK', 'USB_D+_LINK',
)


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


def net_ids(text):
    out = {}
    for nid, name in re.findall(r'\(net\s+(\d+)\s+"([^"]+)"\)', text):
        out[name] = nid
    return out


IDS = net_ids(s)


def block_has_net(block, name):
    if f'(net "{name}")' in block:
        return True
    nid = IDS.get(name)
    return bool(nid and re.search(rf'\(net\s+{re.escape(nid)}\)', block))


def remove_usb_copper(text, token):
    ranges = []
    pos = 0
    needle = '(' + token
    while True:
        i = text.find(needle, pos)
        if i < 0:
            break
        j = balanced_block(text, i)
        blk = text[i:j]
        if any(block_has_net(blk, n) for n in USB_NETS):
            ranges.append((i, j))
        pos = j
    for i, j in reversed(ranges):
        text = text[:i] + text[j:]
    return text


s = remove_usb_copper(s, 'segment')
s = remove_usb_copper(s, 'via')


def seg(net, layer, x1, y1, x2, y2, width=.20):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {width:.3f}) '
            f'(layer "{layer}") (net "{net}"))')


def via(net, x, y, size=.55, drill=.30):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") '
            f'(net "{net}"))')


r = []

# MCU side: direct F.Cu connections from ESP32-C6 GPIO12/13 to the 22R series
# resistors. No layer changes before the resistors.
r += [
    seg('USB_D-_LINK_MCU','F.Cu',73.250,23.180,70.700,23.180),
    seg('USB_D-_LINK_MCU','F.Cu',70.700,23.180,69.500,23.000),
    seg('USB_D-_LINK_MCU','F.Cu',69.500,23.000,67.500,23.000),

    seg('USB_D+_LINK_MCU','F.Cu',73.250,24.450,71.000,24.450),
    seg('USB_D+_LINK_MCU','F.Cu',71.000,24.450,70.000,25.000),
    seg('USB_D+_LINK_MCU','F.Cu',70.000,25.000,67.500,25.000),
]

# Resistor-to-ESD trunk. After the short resistor escape, D+/D- run together
# on F.Cu with 0.42 mm centre spacing (0.22 mm copper-to-copper gap at 0.20 mm
# trace width), respecting the board's 0.20 mm clearance rule.
r += [
    # D+
    seg('USB_D+_LINK','F.Cu',68.500,25.000,68.800,25.600),
    seg('USB_D+_LINK','F.Cu',68.800,25.600,68.800,30.800),
    seg('USB_D+_LINK','F.Cu',68.800,30.800,69.600,31.400),
    seg('USB_D+_LINK','F.Cu',69.600,31.400,72.000,31.400),
    seg('USB_D+_LINK','F.Cu',72.000,31.400,84.900,31.400),
    seg('USB_D+_LINK','F.Cu',84.900,31.400,86.333,31.400),

    # D-
    seg('USB_D-_LINK','F.Cu',68.500,23.000,69.300,23.600),
    seg('USB_D-_LINK','F.Cu',69.300,23.600,69.300,30.900),
    seg('USB_D-_LINK','F.Cu',69.300,30.900,69.900,31.820),
    seg('USB_D-_LINK','F.Cu',69.900,31.820,72.000,31.820),
    seg('USB_D-_LINK','F.Cu',72.000,31.820,84.900,31.820),
    seg('USB_D-_LINK','F.Cu',84.900,31.820,85.800,31.950),
    seg('USB_D-_LINK','F.Cu',85.800,31.950,87.253,32.320),
]

# ESD-to-connector main A-side pair. The two main conductors stay on F.Cu.
r += [
    # D+
    seg('USB_D+_LINK','F.Cu',86.333,31.400,87.500,30.900),
    seg('USB_D+_LINK','F.Cu',87.500,30.900,88.800,30.050),
    seg('USB_D+_LINK','F.Cu',88.800,30.050,89.800,29.350),
    seg('USB_D+_LINK','F.Cu',89.800,29.350,90.400,28.850),
    seg('USB_D+_LINK','F.Cu',90.400,28.850,92.645,28.850),

    # D-
    seg('USB_D-_LINK','F.Cu',87.253,32.320,88.100,31.600),
    seg('USB_D-_LINK','F.Cu',88.100,31.600,89.300,30.650),
    seg('USB_D-_LINK','F.Cu',89.300,30.650,90.100,29.850),
    seg('USB_D-_LINK','F.Cu',90.100,29.850,90.800,29.350),
    seg('USB_D-_LINK','F.Cu',90.800,29.350,92.645,29.350),
]

# Type-C mirrored B-side contacts. One short B.Cu bridge per signal avoids
# crossing the interleaved D+/D- contact order on the connector row.
r += [
    # D+ duplicate (J3 pad at y=27.85) -> main D+ at y=28.85
    seg('USB_D+_LINK','F.Cu',92.645,27.850,90.600,27.750),
    via('USB_D+_LINK',90.600,27.750),
    seg('USB_D+_LINK','B.Cu',90.600,27.750,89.500,28.850),
    via('USB_D+_LINK',89.500,28.850),
    seg('USB_D+_LINK','F.Cu',89.500,28.850,90.400,28.850),

    # D- duplicate (J3 pad at y=28.35) -> main D- at y=29.35
    seg('USB_D-_LINK','F.Cu',92.645,28.350,91.500,28.350),
    via('USB_D-_LINK',91.500,28.350),
    seg('USB_D-_LINK','B.Cu',91.500,28.350,90.600,29.350),
    via('USB_D-_LINK',90.600,29.350),
    seg('USB_D-_LINK','F.Cu',90.600,29.350,90.800,29.350),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied final low-transition USB differential routing to {P}')
