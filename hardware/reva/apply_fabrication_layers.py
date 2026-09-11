from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Production-metadata pass only: declare the standard solder-mask layers in
# the hand-generated KiCad board.  This does not change copper, nets, pads,
# routing, placement, or the physical stackup.  KiCad will not emit mask
# Gerbers for layers that are absent from the board layer table, even when
# they are requested explicitly on the CLI.

if '"F.Mask"' not in s or '"B.Mask"' not in s:
    pat = r'(?m)^(\s*\(2\s+"B\.Cu"\s+signal\)\s*)$'
    m = re.search(pat, s)
    if not m:
        raise RuntimeError('B.Cu layer declaration not found')
    indent = re.match(r'\s*', m.group(1)).group(0)
    add = (
        m.group(1) + '\n'
        + indent + '(1 "F.Mask" user)' + '\n'
        + indent + '(3 "B.Mask" user)'
    )
    s = s[:m.start()] + add + s[m.end():]

if '"F.Mask"' not in s or '"B.Mask"' not in s:
    raise RuntimeError('failed to declare solder-mask layers')

P.write_text(s, encoding='utf-8')
print(f'Declared F.Mask/B.Mask fabrication layers in {P}')
