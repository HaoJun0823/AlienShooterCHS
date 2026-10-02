path = "I:/LocalGames/AlienShooter v1.22DIC/Font/001font.tga"
d = open(path, 'rb').read()
W, H, BPP, OFF = 208, 288, 3, 18
CW, CH = 13, 18
COLS, ROWS = 16, 16

def px(x, y):
    p = OFF + (y * W + x) * BPP
    return d[p] + d[p + 1] + d[p + 2]

def bitmap(ci):
    gx, gy = ci % COLS, ci // COLS
    cx, cy = gx * CW, gy * CH
    return [''.join('#' if px(cx + x, cy + y) > 60 else '.' for x in range(CW)) for y in range(12)]

# hypothesis A: code = cell + 0x21
# cells 0x00..0x0F -> codes 0x21..0x30  => '!"#$%&\'()*+,-./0'
for start in (0x00, 0x0F, 0x10, 0x1F, 0x2F, 0x3F, 0x5F, 0x6F, 0x7F, 0x8F, 0x9F, 0xAF, 0xBF, 0xCF, 0xDF, 0xEF):
    code = start + 0x21
    lbl = repr(chr(code)) if 32 <= code < 127 else '?'
    print('=== cell %02X -> code %02X %s ===' % (start, code, lbl))
    for yy in range(12):
        print('  ' + bitmap(start)[yy])
    print()
