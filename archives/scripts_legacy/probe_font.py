import sys

# 001font.tga: 208x288, 24bpp, cell 13x18, 16 cols
# 实测: cell index 0x10..0x1F 显示 '0'-'9'  => glyph code = cell_index - 0x10
# 推论: 图集从 ASCII 0x20(空格) 开始? 不, 0x10格显示'0'
# 需实证: 渲染全部, 与标准 ASCII 表比对
path = "I:/LocalGames/AlienShooter v1.22DIC/Font/001font.tga"
d = open(path, 'rb').read()
W, H, BPP, OFF = 208, 288, 3, 18
CW, CH = 13, 18
COLS = W // CW      # 16
ROWS = H // CH      # 16

def px(x, y):
    p = OFF + (y * W + x) * BPP
    return d[p] + d[p + 1] + d[p + 2]

def bitmap(cell_index):
    gx = cell_index % COLS
    gy = cell_index // COLS
    cx, cy = gx * CW, gy * CH
    return [''.join('1' if px(cx + x, cy + y) > 60 else '0' for x in range(CW)) for y in range(12)]

def sig(cell_index):
    return ''.join(bitmap(cell_index))

# 统计每格墨迹量, 找出空白格
counts = []
for ci in range(COLS * ROWS):
    n = sum(r.count('1') for r in bitmap(ci))
    counts.append(n)

print('cell ink counts (16x16 grid, cell_index = row*16+col):')
for r in range(ROWS):
    print('%2X: ' % r + ' '.join('%3d' % counts[r * COLS + c] for c in range(COLS)))
print()
print('empty cells (<=2 ink):', [hex(i) for i, c in enumerate(counts) if c <= 2])
