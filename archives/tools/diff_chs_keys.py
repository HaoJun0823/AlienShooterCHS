#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""对比 data_original 与 data_chs 下所有 ini/txt 的 key 完整性。
   目标：抓出「汉化后 key 丢失 / 值变空 / 值仍为英文」的条目
         —— 这些正是「英文版有提示、汉化版没有」的典型成因。
"""
import os, re, sys, collections

A = r'G:/Projects/AlienShooterCHS/data_original'
B = r'G:/Projects/AlienShooterCHS/data_chs'

def read(p):
    for enc in ('utf-8-sig', 'gbk', 'cp1251', 'latin1'):
        try:
            return open(p, encoding=enc).read()
        except Exception:
            pass
    return open(p, 'rb').read().decode('latin1')

def parse_ini(s):
    """返回 [(section, key, value)] 保序"""
    out = []
    cur = ''
    for ln in s.splitlines():
        t = ln.strip()
        if not t or t.startswith(';'):
            continue
        if t.startswith('[') and t.endswith(']'):
            cur = t[1:-1]
            continue
        if '=' in t:
            k, v = t.split('=', 1)
            out.append((cur, k.strip(), v))
    return out

def has_cjk(s):
    return any(ord(c) > 0x2E80 for c in s)

print('=' * 78)
print('=== strings.ini 对比 ===')
print('=' * 78)
pa, pb = os.path.join(A, 'strings.ini'), os.path.join(B, 'strings.ini')
ea, eb = parse_ini(read(pa)), parse_ini(read(pb))
print('原始 %d 条 / 汉化 %d 条' % (len(ea), len(eb)))

ka = collections.Counter((s, k) for s, k, v in ea)
kb = {s: {} for s, k, v in ea}
db = {}
for s, k, v in eb:
    db[(s, k)] = v

missing = [(s, k) for (s, k) in ka if (s, k) not in db]
print('\n[1] 汉化版缺失的 key：%d' % len(missing))
for s, k in missing[:40]:
    va = dict(((ss, kk), vv) for ss, kk, vv in ea).get((s, k), '')
    print('    [%s] %-20s 原文="%s"' % (s, k, va[:60]))

empty = [(s, k) for (s, k) in ka if (s, k) in db and db[(s, k)].strip() == '']
notempty_orig = [(s, k) for (s, k) in empty
                 if dict(((ss, kk), vv) for ss, kk, vv in ea).get((s, k), '').strip() != '']
print('\n[2] 原文非空但汉化后为空：%d' % len(notempty_orig))
for s, k in notempty_orig[:40]:
    print('    [%s] %-20s 原文="%s"' % (s, k, dict(((ss, kk), vv) for ss, kk, vv in ea).get((s, k), '')[:60]))

unchanged = [(s, k) for (s, k) in ka if (s, k) in db and db[(s, k)] == dict(((ss, kk), vv) for ss, kk, vv in ea).get((s, k), '')]
print('\n[3] 值完全未改（可能漏翻）：%d' % len(unchanged))
for s, k in unchanged[:60]:
    v = db[(s, k)]
    print('    [%s] %-22s "%s"' % (s, k, v[:60]))

print('\n' + '=' * 78)
print('=== Text/*.txt 对比 ===')
print('=' * 78)
ta = set(os.listdir(os.path.join(A, 'Text'))) if os.path.isdir(os.path.join(A, 'Text')) else set()
tb = set(os.listdir(os.path.join(B, 'Text'))) if os.path.isdir(os.path.join(B, 'Text')) else set()
print('原始 %d 个 / 汉化 %d 个' % (len(ta), len(tb)))
print('仅原始有：', sorted(ta - tb)[:20])
print('仅汉化有：', sorted(tb - ta)[:20])

print('\n--- 汉化版内容为空 / 明显变短的文件 ---')
for f in sorted(ta & tb):
    try:
        a = open(os.path.join(A, 'Text', f), 'rb').read()
        b = open(os.path.join(B, 'Text', f), 'rb').read()
    except Exception:
        continue
    if len(b) == 0 and len(a) > 0:
        print('  [空!] %-28s 原 %d -> 汉 %d' % (f, len(a), len(b)))
    elif len(a) > 0 and len(b) < len(a) * 0.5:
        print('  [短]  %-28s 原 %d -> 汉 %d' % (f, len(a), len(b)))
