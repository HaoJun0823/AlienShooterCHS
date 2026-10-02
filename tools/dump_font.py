import sys

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

# 渲染有内容的三个区段: 0x00-0x3F, 0x80-0xAF, 0xB0-0xCF, 0xD1-0xDF
for start, end in [(0x00, 0x10), (0x10, 0x20), (0x20, 0x30), (0x30, 0x40),
                   (0x80, 0x90), (0x90, 0xA0), (0xA0, 0xB0), (0xB0, 0xC0),
                   (0xC0, 0xD0), (0xD0, 0xE0)]:
    print('=== cells %02X-%02X ===' % (start, end - 1))
    print('  ' + ' | '.join(('%02X' % i).center(CW) for i in range(start, end)))
    for yy in range(12):
        print('  ' + ' | '.join(bitmap(i)[yy] for i in range(start, end)))
    print()
