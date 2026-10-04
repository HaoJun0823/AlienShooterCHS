#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
验证 screen+0xE34（字形渲染器槽）是否被赋值。
扫 .text 找所有写 [reg+0E34h] 的指令；再找 sub_41B710 的调用者。
"""
import struct
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
blob=d[T['rp']:T['rp']+T['rsz']]
md=Cs(CS_ARCH_X86, CS_MODE_32); md.detail=True

IMG=0x400000
BASE=IMG+T['va']      # .text 的 VA 基址
print('=== 写 [reg+0E34h] 的指令 ===')
# 编码: C7 8x 34 0E 00 00 (mov dword [reg+disp32], imm32)
#       89 8x 34 0E 00 00 (mov [reg+disp32], reg)
#   mod=10(disp32) ⇒ modrm & 0xC0 == 0x80
n=0
for i in range(len(blob)-7):
    b0,b1=blob[i],blob[i+1]
    if b0 in (0xC7,0x89) and (b1 & 0xC0)==0x80:
        disp=struct.unpack_from('<I',blob,i+2)[0]
        if disp==0xE34:
            print('  0x%06X  %s'%(BASE+i, ' '.join('%02X'%x for x in blob[i:i+7])))
            n+=1
if n==0: print('  （无）')
print('  合计 %d 条'%n)

print()
print('=== 读 [reg+0E34h] 的指令 (8B mod=10 / 83 mod=10 / FF mod=10) ===')
n=0
for i in range(len(blob)-7):
    b0,b1=blob[i],blob[i+1]
    if b0 in (0x8B,0x83,0xFF,0x3B,0x39) and (b1 & 0xC0)==0x80:
        disp=struct.unpack_from('<I',blob,i+2)[0]
        if disp==0xE34:
            print('  0x%06X  %s'%(BASE+i, ' '.join('%02X'%x for x in blob[i:i+7])))
            n+=1
print('  合计 %d 条'%n)

print()
print('=== call sub_41B710 的调用点（修正 VA 口径）===')
n=0
for i in range(len(blob)-5):
    if blob[i]!=0xE8: continue
    rel=struct.unpack_from('<i',blob,i+1)[0]
    here=BASE+i
    if here+5+rel==0x41B710:
        print('  0x%06X'%here); n+=1
print('  合计 %d 条'%n)

print()
print('=== sub_4306A0 全函数反汇编 ===')
off=T['rp']+(0x4306A0-IMG-T['va'])
for ins in md.disasm(d[off:off+0x60], 0x4306A0):
    print('  %08X  %-22s %s %s'%(ins.address,' '.join('%02X'%b for b in ins.bytes),ins.mnemonic,ins.op_str))
