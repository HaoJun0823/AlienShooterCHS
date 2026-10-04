#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""加壳判定：入口点反汇编 / 已知壳签名 / 各段熵曲线 / 导入表完整性。"""
import sys, struct, math
from collections import Counter

def u16(d,o): return struct.unpack_from('<H',d,o)[0]
def u32(d,o): return struct.unpack_from('<I',d,o)[0]

path = sys.argv[1]
d = open(path,'rb').read()
pe = u32(d,0x3C)
nsec=u16(d,pe+6); opt=pe+24
entry=u32(d,opt+16); base=u32(d,opt+28)
sec=opt+u16(d,pe+20)

def r2o(rva):
    for i in range(nsec):
        b=sec+40*i
        va=u32(d,b+12); vsz=u32(d,b+8); rp=u32(d,b+20); rsz=u32(d,b+16)
        if va<=rva<va+max(vsz,rsz): return rp+(rva-va)
    return None

print('file=%s size=%d'%(path,len(d)))
print('entry RVA=0x%08X -> file off 0x%X'%(entry, r2o(entry) or -1))

eo=r2o(entry)
print('\n--- 入口点前 96 字节 ---')
blob=d[eo:eo+96]
print(' '.join('%02X'%b for b in blob))
print('\n--- 入口点后 256 字节 (若可读) ---')
blob2=d[eo+96:eo+352]
print(' '.join('%02X'%b for b in blob2))

print('\n--- 已知壳签名 ---')
sigs = {
 'UPX':[b'UPX0',b'UPX1',b'UPX!',b'UPX2'],
 'ASPack':[b'.aspack',b'.adata',b'ASPack'],
 'PECompact':[b'PEC2',b'PECompact2'],
 'Themida':[b'Themida',b'.themida'],
 'VMProtect':[b'.vmp0',b'.vmp1',b'VMProtect'],
 'Obsidium':[b'Obsidium',b'.obsidiu'],
 'Armadillo':[b'.text1',b'Armadillo'],
 'tElock':[b'tElock',b'.petite'],
 'MPRESS':[b'MPRESS1',b'MPRESS2',b'.MPRESS'],
 'Enigma':[b'Enigma protector',b'.enigma'],
 'PESpin':[b'PESpin'],
 'Yoda':[b"yoda's Protector",b'.yP'],
 'ExeCryptor':[b'ExeCryptor'],
 'MEW':[b'MEW'],
 'FSG':[b'FSG!'],
 'NsPack':[b'.nsp0',b'.nsp1',b'nsPack'],
 'MEW/FSG':[b'MEW11',b'MEW10'],
}
for name, arr in sigs.items():
    hits=[s.decode('latin1') for s in arr if s in d]
    if hits: print('  *** %-12s : %s'%(name, hits))

# 各段熵曲线（每 4KB）
print('\n--- 代码段 4KB 熵曲线（检测是否有低熵引导区） ---')
for i in range(nsec):
    b=sec+40*i
    nm=d[b:b+8].rstrip(b'\x00').decode('latin1')
    va=u32(d,b+12); vsz=u32(d,b+8); rp=u32(d,b+20); rsz=u32(d,b+16)
    if rsz==0: continue
    data=d[rp:rp+rsz]
    ent=[]
    for j in range(0, len(data), 4096):
        c=data[j:j+4096]
        if not c: continue
        n=len(c); cc=Counter(c)
        ent.append(-sum(v/n*math.log2(v/n) for v in cc.values()))
    lo=min(ent); lo_i=ent.index(lo)
    print('  %-9s va=%08X rsz=%08X  minH=%.3f@4K#%d  maxH=%.3f  avgH=%.3f'%(
        nm,va,rsz,lo,lo_i,max(ent),sum(ent)/len(ent)))
    print('     曲线(每8KB取样): '+' '.join('%.1f'%x for x in ent[::8]))

# 导入表完整性：真正未加壳的 exe 每个 dll 的函数数不会是 1
print('\n--- 导入函数总数（正常 exe 应 >100）---')
imp=u32(d,opt+104)
io=r2o(imp); tot=0; dlls=[]
i=0
while True:
    o=io+20*i
    nm=u32(d,o+12); ft=u32(d,o+16)
    if nm==0 and ft==0: break
    no=r2o(nm); name=d[no:d.index(b'\x00',no)].decode('latin1')
    to=r2o(ft); k=0
    while u32(d,to+4*k)!=0: k+=1
    tot+=k; dlls.append((name,k))
    i+=1
print('  DLL=%d  导入函数总数=%d'%(len(dlls),tot))
for n,k in dlls:
    print('    %-16s %d %s'%(n,k,'  <-- 可疑(仅1个)' if k<=1 else ''))

# 检查是否有可读字符串（未加壳应有大量 ASCII/UTF16 串）
print('\n--- 明文串抽样（判断能否离线静态分析）---')
import re
asc=re.findall(rb'[\x20-\x7e]{8,}',d)
u16s=re.findall(rb'(?:[\x20-\x7e]\x00){6,}',d)
print('  ASCII>=8 : %d 条  样例: %s'%(len(asc), [s[:40].decode('latin1') for s in asc[:12]]))
print('  UTF16>=6 : %d 条  样例: %s'%(len(u16s), [s.decode('utf-16-le')[:30] for s in u16s[:12]]))
