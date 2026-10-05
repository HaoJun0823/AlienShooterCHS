#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""解析 chs_probe.log
   日志内的文本是 GBK(cp936)，先前按 latin1 解码导致乱码。
   这里按 GBK 回解，并做真实去重，重点找：
     - 道具提示（Item*/获得/拾取）
     - 结算界面（本关完成/成绩/关卡时间）
"""
import sys, re, collections

path = sys.argv[1] if len(sys.argv) > 1 else r"I:/LocalGames/AlienShooter v1.22DIC/update/chs_probe.log"
raw = open(path, 'rb').read()
lines = raw.split(b'\r\n')

def dec(b):
    for enc in ('utf-8', 'gbk', 'cp936'):
        try:
            s = b.decode(enc)
            if '\ufffd' not in s:
                return s
        except Exception:
            pass
    return b.decode('latin1')

bxs, bx0s, emits = [], [], []
others = []
for ln in lines:
    if not ln:
        continue
    t = dec(ln)
    m = re.match(r'^\[([A-Za-z0-9# ]+)\]', t)
    tag = m.group(1) if m else ''
    if tag == 'bx':
        bxs.append(t)
    elif tag == 'bx0':
        bx0s.append(t)
    elif tag == 'emit':
        emits.append(t)
    else:
        others.append(t)

print('[bx]=%d  [bx0]=%d  [emit]=%d  other=%d' % (len(bxs), len(bx0s), len(emits), len(others)))

def txt(s):
    m = re.search(r'\| "(.*)"\s*$', s)
    return m.group(1).strip() if m else ''

print('\n=== [bx] 去重后的文本（按出现顺序） ===')
seen = []
for t in bxs:
    v = txt(t)
    if v and v not in seen:
        seen.append(v)
print('共 %d 个不同文本：' % len(seen))
for v in seen:
    print('   ' + v)

print('\n=== [bx0] 去重后的文本（未接管的 ASCII） ===')
seen0 = []
for t in bx0s:
    v = txt(t)
    if v and v not in seen0:
        seen0.append(v)
print('共 %d 个不同文本：' % len(seen0))
for v in seen0[:80]:
    print('   ' + v)

print('\n=== [emit] 去重后的文本 ===')
seene = []
for t in emits:
    m = re.search(r't="(.*?)" at=', t)
    v = m.group(1).strip() if m else ''
    if v and v not in seene:
        seene.append(v)
print('共 %d 个不同文本：' % len(seene))
for v in seene[:80]:
    print('   ' + v)

# 关键：找道具/结算关键词
print('\n=== 关键词命中搜索 ===')
kw = ['获得', '拾取', '得到', '装备', '物品', '道具', '完成', '成绩', '关卡时间',
      'Item', 'item', 'Taken', 'Found', 'Picked', 'Score', 'Time']
for k in kw:
    hit = [t for t in bxs + bx0s + emits if k in t]
    if hit:
        print('  「%s」命中 %d 条：' % (k, len(hit)))
        for h in hit[:5]:
            print('      ' + h[:150])
