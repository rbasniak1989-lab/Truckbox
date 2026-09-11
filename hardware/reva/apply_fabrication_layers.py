from pathlib import Path
import re

P = Path(__file__).with_name('TruckBox_RevA.kicad_pcb')
s = P.read_text(encoding='utf-8')

# Production-metadata pass only: declare the standard solder-mask layers in
# the hand-generated KiCad board. This edits ONLY the top-level (layers ...)
# table. Copper, nets, pads, routing, placement and physical stackup remain
# untouched.
#
# KiCad 9/10 current layer IDs used by this generated board:
#   1 = F.Mask
#   3 = B.Mask


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


# Find the top-level layer table, and inspect ONLY that table. Pad definitions
# also contain the strings F.Mask/B.Mask and must not be mistaken for enabled
# board layers.
i = s.find('(layers')
if i < 0:
    raise RuntimeError('top-level layers block not found')
j = balanced_block(s, i)
blk = s[i:j]

has_fmask = re.search(r'\(1\s+"F\.Mask"\s+user(?:\s|\))', blk) is not None
has_bmask = re.search(r'\(3\s+"B\.Mask"\s+user(?:\s|\))', blk) is not None

if not (has_fmask and has_bmask):
    # Infer the existing indentation from any layer row. The order of enabled
    # non-copper rows is not electrically significant; KiCad normalizes the
    # table when the board is saved by the subsequent DRC pass.
    im = re.search(r'\n([ \t]+)\(\d+\s+"[^"]+"', blk)
    indent = im.group(1) if im else '\t\t'
    rows = []
    if not has_fmask:
        rows.append(indent + '(1 "F.Mask" user)')
    if not has_bmask:
        rows.append(indent + '(3 "B.Mask" user)')

    close = blk.rfind(')')
    if close < 0:
        raise RuntimeError('layers block closing paren not found')
    before = blk[:close].rstrip()
    # Match the indentation of the '(layers' closing paren itself.
    close_indent = re.search(r'\n([ \t]*)$', blk[:close])
    parent_indent = close_indent.group(1) if close_indent else '\t'
    blk = before + '\n' + '\n'.join(rows) + '\n' + parent_indent + ')'
    s = s[:i] + blk + s[j:]

# Re-read only the resulting layer table for a strict postcondition.
i2 = s.find('(layers')
j2 = balanced_block(s, i2)
blk2 = s[i2:j2]
if re.search(r'\(1\s+"F\.Mask"\s+user(?:\s|\))', blk2) is None:
    raise RuntimeError('F.Mask layer declaration missing after patch')
if re.search(r'\(3\s+"B\.Mask"\s+user(?:\s|\))', blk2) is None:
    raise RuntimeError('B.Mask layer declaration missing after patch')

P.write_text(s, encoding='utf-8')
print(f'Declared F.Mask/B.Mask fabrication layers in {P}')
