#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""反汇编 vtable 构造点 0x434FDF 所在函数；搜 Item 字符串；修正 vtable 引用扫描。"""
import struct, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

path='I:/LocalGames/AlienShooter v1.22DIC/AlienShooter.exe'
d=open(path,'rb').read()
def u16(o): return struct.unpack_from('<H',d,o)[0]
def u32(o): return struct.unpack_from('<I',d,o)[0]
pe=u32(0x3C);nsec=u16(pe+6);opt=pe+24;sec=opt+u16(pe+20)
secs=[]
for i in range(nsec):
    b=sec+40*i
    secs.append(dict(name=d[b:b+8].rstrip(b'\x00').decode('latin1'),
                     va=u32(b+12),vsz=u32(b+8),rp=u32(b+20),rsz=u32(b+16),ch=u32(b+36)))
T=[s for s in secs if s['name']=='.text'][0]
IMG=0x400000
def r2o(rva):
    for s in secs:
        if s['va']<=rva<s['va']+max(s['vsz'],s['rsz']): return s['rp']+(rva-s['va'])
    return None
def o2rva(off):
    for s in secs:
        if s['rp']<=off<s['rp']+s['rsz']: return s['va']+(off-s['rp'])
    return None
md=Cs(CS_ARCH_X86, CS_MODE_32)

print('=== 0x434FDF 所在函数（往前找序言）===')
# 往前找 55 8B EC
start=None
for k in range(0x400):
    o=r2o(0x434FDF-IMG-k)
    if o and d[o]==0x55 and d[o+1]==0x8B and d[o+2]==0xEC:
        start=0x434FDF-k; break
print('  函数序言 = 0x%s'%('%06X'%start if start else '?'))
if start:
    o=r2o(start-IMG)
    for ins in md.disasm(d[o:o+0x140], start):
        print('  %08X  %-22s %s %s'%(ins.address,' '.join('%02X'%b for b in ins.bytes),ins.mnemonic,ins.op_str))

print()
print('=== 搜 "Item" 相关字符串 ===')
for kw in [b'Item',b'item',b'ITEM',b'%d',b'Get item',b'GetItem',b'FindSecret']:
    hits=[m.start() for m in re.finditer(re.escape(kw),d)]
    rv=[o2rva(h) for h in hits]
    print('  %-12s x%d  %s'%(kw.decode('latin1'),len(hits),
        ', '.join('0x%06X'%x if x else '?' for x in rv[:8])))

print()
print('=== 修正：搜所有 imm32 = 0x4da7c0 / 0x4da7d0 的位置（不做指令边界验证）===')
blob=d[T['rp']:T['rp']+T['rsz']]
for target in (0x4da7c0,0x4da7d0):
    found=[]
    for i in range(len(blob)-4):
        if struct.unpack_from('<I',blob,i)[0]==target:
            found.append(IMG+T['va']+i)
    print('  0x%06X  出现 %d 次: %s'%(target,len(found),', '.join('0x%06X'%f for f in found[:10])))
