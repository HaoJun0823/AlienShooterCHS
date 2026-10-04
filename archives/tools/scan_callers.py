#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
扫描 v1.22DIC exe：
1) RVA 0x26680（文本块绘制主循环）的所有直接调用点（E8 rel32）
2) RVA 0x306A0（sub_4306A0）的所有调用点
3) 找出 .text 里所有 "call dword ptr [reg+disp32]" 的 disp 聚类（虚表槽用法）
"""
import sys, struct
from collections import Counter, defaultdict

def u16(d,o): return struct.unpack_from('<H',d,o)[0]
def u32(d,o): return struct.unpack_from('<I',d,o)[0]

path='I:/LocalGames/AlienShooter v1.22DIC/AlienShooter.exe'
d=open(path,'rb').read()
pe=u32(d,0x3C); nsec=u16(d,pe+6); opt=pe+24
base=u32(d,opt+28)
sec=opt+u16(d,pe+20)
secs=[]
for i in range(nsec):
    b=sec+40*i
    secs.append(dict(name=d[b:b+8].rstrip(b'\x00').decode('latin1'),
                     va=u32(d,b+12),vsz=u32(d,b+8),rp=u32(d,b+20),rsz=u32(d,b+16),
                     ch=u32(d,b+36)))
print('ImageBase=0x%X'%base)
for s in secs: print('  %-9s va=%08X vsz=%08X rsz=%08X ch=%08X'%(s['name'],s['va'],s['vsz'],s['rsz'],s['ch']))

def r2o(rva):
    for s in secs:
        if s['va']<=rva<s['va']+max(s['vsz'],s['rsz']): return s['rp']+(rva-s['va'])
    return None

def scan_call(target_rva):
    """扫所有 E8 rel32 指向 target_rva 的调用点"""
    hits=[]
    for s in secs:
        if not (s['ch']&0x20000000): continue
        blob=d[s['rp']:s['rp']+s['rsz']]
        for i in range(len(blob)-5):
            if blob[i]!=0xE8: continue
            rel=struct.unpack_from('<i',blob,i+1)[0]
            here=s['va']+i
            tgt=here+5+rel
            if tgt==target_rva:
                hits.append(here)
    return hits

for name,rva in [('DrawText 0x26680',0x26680),('sub_4306A0 0x306A0',0x306A0),
                 ('FindGlyph 0x17F80',0x17F80),('HasGlyph 0x17F50',0x17F50),
                 ('FontLoad 0x4A6A0',0x4A6A0)]:
    hs=scan_call(rva)
    print('\n--- call %s ---  共 %d 处'%(name,len(hs)))
    for h in hs[:40]: print('   0x%06X'%h)

# 虚表槽聚类：call dword ptr [reg+disp32] = FF 90-97 disp32 （reg=eax..edi）
print('\n\n=== call [reg+disp32] 的 disp 聚类 (top 30) ===')
cnt=Counter()
for s in secs:
    if not (s['ch']&0x20000000): continue
    blob=d[s['rp']:s['rp']+s['rsz']]
    i=0
    while i < len(blob)-6:
        op=blob[i]; modrm=blob[i+1]
        # FF /2 = call, mod=10 (disp32), rm=000..111
        if op==0xFF and (modrm&0xC7)==0x90:
            disp=struct.unpack_from('<I',blob,i+2)[0]
            cnt[disp]+=1
            i+=6; continue
        i+=1
for disp,n in cnt.most_common(30):
    print('   +0x%03X  x%d'%(disp,n))
