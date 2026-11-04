#!/usr/bin/env python3
"""Build the Xbox button-label patch for BIN/OPTION/NET00.RAW.

The network screens' key hints (F1..F8, Esc) are baked into NET00.RAW, a
256x256 RGB atlas from the PC game. This script repaints them with Xbox pad
names using the atlas's own font, and writes the changed pixels as a C header
(include/xbox_net00_patch.h) that the Xbox build applies when the game loads
the file. The original data is never modified.

usage: make_net00_patch.py NET00.RAW [preview.png]
"""
import sys

W = H = 256
src = bytearray(open(sys.argv[1], 'rb').read())
assert len(src) == W * H * 3
img = bytearray(src)

def get(x, y):
    i = (y * W + x) * 3
    return tuple(img[i:i + 3])

def put(x, y, c):
    i = (y * W + x) * 3
    img[i:i + 3] = bytes(c)

def is_key(c):
    return c[0] < 40 and c[1] > 200 and c[2] < 40

GREEN = (0, 255, 0)

# Uppercase font, row y=208..216 (9 px), from the atlas.
UPPER_X = [(0,5),(6,5),(12,4),(17,5),(23,5),(29,5),(35,5),(41,5),(47,4),(52,4),(57,5),(63,4),(68,8),
           (77,6),(84,5),(90,5),(96,5),(102,5),(108,5),(114,6),(121,5),(127,5),(133,8),(142,5),(148,5),(154,5)]
GLYPH_H = 9

def glyph_mask(ch):
    """Return (w, rows) with rows[y][x] = True where the glyph has ink."""
    if ch == '<':
        x0, w, y0 = 48, 5, 188
    elif ch == '>':
        x0, w, y0 = 54, 5, 188
    elif ch in '^v':
        # hand-drawn 5x9 arrows
        up = ["..#..", ".###.", "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "....."]
        rows = [[c == '#' for c in r] for r in (up if ch == '^' else up[::-1][1:] + ["....."])]
        return 5, rows
    else:
        x0, w = UPPER_X[ord(ch.upper()) - ord('A')]
        y0 = 208
    rows = [[not is_key(tuple(src[((y0 + y) * W + x0 + x) * 3:((y0 + y) * W + x0 + x) * 3 + 3]))
             for x in range(w)] for y in range(GLYPH_H)]
    return w, rows

def glyph_color(ch, x, y):
    """Colour of the source font pixel (orange gradient) for arrows/letters."""
    if ch in '^v<>':
        return [(255, 200, 80), (255, 190, 70), (255, 175, 60), (250, 160, 50), (245, 145, 45),
                (240, 130, 40), (235, 115, 35), (230, 100, 30), (225, 90, 25)][y]
    if False:
        pass
    else:
        x0, _ = UPPER_X[ord(ch.upper()) - ord('A')]
        y0 = 208
    i = ((y0 + y) * W + x0 + x) * 3
    return tuple(src[i:i + 3])

def text_width(s):
    return sum(glyph_mask(c)[0] for c in s) + (len(s) - 1)

def draw_text(s, x, y, color=None, shadow=(20, 10, 0)):
    """Draw s with its top-left at (x, y); 1-px drop shadow down-right."""
    for pass_ in (0, 1):
        cx = x
        for ch in s:
            w, rows = glyph_mask(ch)
            for gy in range(GLYPH_H):
                for gx in range(w):
                    if rows[gy][gx]:
                        if pass_ == 0:
                            put(cx + gx + 1, y + gy + 1, shadow)
                        else:
                            put(cx + gx, y + gy, color if color else glyph_color(ch, gx, gy))
            cx += w + 1

def erase_with_column(x0, y0, x1, y1, col):
    """Paint [x0,x1)x[y0,y1) with the pixels of column `col` (keeps the
    vertical shading of capsules and bars)."""
    for y in range(y0, y1):
        c = get(col, y)
        for x in range(x0, x1):
            put(x, y, c)

# ---- blue F-key capsules: 40x16 each, F1..F6 at y=28, F7..F12 at y=44 ----
CAPS = {
    'F1': (0, 28, 'A'), 'F2': (40, 28, 'RT'), 'F3': (80, 28, 'Y'),
    'F6': (200, 28, '<>'), 'F7': (0, 44, 'LT'), 'F8': (40, 44, '^v'),
}
for name, (cx, cy, label) in CAPS.items():
    erase_with_column(cx + 9, cy + 2, cx + 29, cy + 15, cx + 8)
    tw = text_width(label)
    tx = cx + 19 - tw // 2
    draw_text(label, tx, cy + 3)

# ---- green bars: replace the pale-yellow key prefix --------------------
def prefix_box(bx, by, bw=128, bh=16):
    xs, ys = [], []
    for y in range(by + 1, by + bh - 1):
        for x in range(bx + 2, bx + bw - 2):
            r, g, b = get(x, y)
            if r > 190 and g > 180 and b < 170:      # pale yellow key name
                xs.append(x); ys.append(y)
    # keep only the leftmost cluster (the key name), not the word after it
    xs.sort()
    clus = [xs[0]]
    for x in xs[1:]:
        if x - clus[-1] > 4:
            break
        clus.append(x)
    return clus[0], max(clus)

BARS = [  # (bar x, bar y, new prefix)
    (128, 76, 'A'),    # F1 CREATE GAME
    (128, 92, 'RT'),   # F2 JOIN GAME
    (128, 108, 'A'),   # F1 GO!
    (0, 0, 'B'),       # Esc BACK
]
yellow = (255, 236, 130)
for bx, by, label in BARS:
    x0, x1 = prefix_box(bx, by)
    erase_with_column(x0 - 1, by + 2, x1 + 3, by + 14, x0 - 2)
    tw = text_width(label)
    draw_text(label, x1 + 1 - tw, by + 3, color=yellow)

# ---- small "Esc..." hint near the bottom right -> "B..." ----------------
# "Esc" covers x 220..243, y 219..231 (the dots follow from x 244).
for y in range(219, 232):
    for x in range(220, 244):
        put(x, y, GREEN)
draw_text('B', 236, 221)

# ---- emit the diff as runs --------------------------------------------
runs = []
for y in range(H):
    x = 0
    while x < W:
        if img[(y*W+x)*3:(y*W+x)*3+3] != src[(y*W+x)*3:(y*W+x)*3+3]:
            s = x
            while x < W and img[(y*W+x)*3:(y*W+x)*3+3] != src[(y*W+x)*3:(y*W+x)*3+3]:
                x += 1
            runs.append((s, y, bytes(img[(y*W+s)*3:(y*W+x)*3])))
        else:
            x += 1

out = ['/* generated by tools/make_net00_patch.py - do not edit */',
       '/* Xbox button names over the PC key hints in BIN/OPTION/NET00.RAW. */',
       'static const unsigned short xb_net00_runs[][3] = {  /* x, y, length */']
data = []
for x, y, px in runs:
    out.append('  {%d,%d,%d},' % (x, y, len(px) // 3))
    data.extend(px)
out.append('};')
out.append('static const unsigned char xb_net00_pixels[%d] = {' % len(data))
for i in range(0, len(data), 24):
    out.append('  ' + ','.join(str(b) for b in data[i:i+24]) + ',')
out.append('};')
out.append('#define XB_NET00_RUNS %d' % len(runs))
hdr = '/home/claude/sonicr-xbox/sonic-r/source/xbox/include/xbox_net00_patch.h'
open(hdr, 'w').write('\n'.join(out) + '\n')
print('runs', len(runs), 'pixels', len(data) // 3)

if len(sys.argv) > 2:
    from PIL import Image
    Image.frombytes('RGB', (W, H), bytes(img)).resize((1024, 1024), Image.NEAREST).save(sys.argv[2])
