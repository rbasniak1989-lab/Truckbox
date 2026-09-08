from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')
net_id = {name: int(i) for i, name in re.findall(r'^  \(net (\d+) "([^"]+)"\)$', s, re.M)}

# SD chip-select routing pass. Rebuild only SD_CS so all previously validated
# CAN/SPI/FRAM geometry remains untouched.
rebuild = {'SD_CS'}
ids = {net_id[n] for n in rebuild}
new = []
for line in s.splitlines():
    if line.startswith('  (segment ') or line.startswith('  (via '):
        m = re.search(r'\(net (\d+)\)', line)
        if m and int(m.group(1)) in ids:
            continue
    new.append(line)
s = '\n'.join(new) + '\n'

def seg(net, x1, y1, x2, y2, w=.22, layer='F.Cu'):
    return (f'  (segment (start {x1:.3f} {y1:.3f}) '
            f'(end {x2:.3f} {y2:.3f}) (width {w:.3f}) '
            f'(layer "{layer}") (net {net_id[net]}))')

def via(net, x, y, size=.70, drill=.35):
    return (f'  (via (at {x:.3f} {y:.3f}) (size {size:.3f}) '
            f'(drill {drill:.3f}) (layers "F.Cu" "B.Cu") (net {net_id[net]}))')

r = []

# SD_CS: U1 GPIO3/pad26 (26.75,10.48) -> J1 CS pad (43.995,36.55)
# plus R25 pull-up pad (56.5,42.0).
# Escape outward from the ESP32 module before changing layer; do not route under
# the module/antenna keepout. The F.Cu return corridor was checked against the
# run-55 clean board, including the frozen SPI storage routing.
r += [
    seg('SD_CS', 26.75, 10.48, 28.0, 10.48),
    via('SD_CS', 28.0, 10.48),
    seg('SD_CS', 28.0, 10.48, 29.02, 11.5, .22, 'B.Cu'),
    seg('SD_CS', 29.02, 11.5, 30.0, 11.5, .22, 'B.Cu'),
    via('SD_CS', 30.0, 11.5),
    seg('SD_CS', 30.0, 11.5, 30.0, 18.8),
    seg('SD_CS', 30.0, 18.8, 30.8, 19.6),
    seg('SD_CS', 30.8, 19.6, 31.3, 19.6),
    seg('SD_CS', 31.3, 19.6, 32.8, 21.1),
    seg('SD_CS', 32.8, 21.1, 32.8, 23.5),
    seg('SD_CS', 32.8, 23.5, 38.5, 23.5),
    seg('SD_CS', 38.5, 23.5, 39.2, 24.2),
    seg('SD_CS', 39.2, 24.2, 39.2, 26.0),
    seg('SD_CS', 39.2, 26.0, 40.5, 27.3),
    seg('SD_CS', 40.5, 27.3, 40.5, 35.5),
    seg('SD_CS', 40.5, 35.5, 43.995, 35.5),
    seg('SD_CS', 43.995, 35.5, 43.995, 36.55),
]

# Pull-up branch: leave the microSD pad toward the lower-right open corridor,
# then run above the existing 3V3_SD pull-up trace into R25.
r += [
    seg('SD_CS', 43.995, 36.55, 44.5, 37.5),
    seg('SD_CS', 44.5, 37.5, 44.5, 41.0),
    seg('SD_CS', 44.5, 41.0, 56.5, 41.0),
    seg('SD_CS', 56.5, 41.0, 56.5, 42.0),
]

pos = s.rfind('\n)')
if pos < 0:
    raise RuntimeError('final PCB paren not found')
s = s[:pos] + '\n' + '\n'.join(r) + s[pos:]
P.write_text(s, encoding='utf-8')
print(f'Applied SD_CS storage routing pass to {P}')
