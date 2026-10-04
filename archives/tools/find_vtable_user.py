#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""确定 0x4da7c0 附近虚表的范围，并找 .text 中引用它的指令。"""
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
IMG=0x400000
def r2o(rva):
    for s in secs:
        if s['va']<=rva<s['va']+max(s['vsz'],s['rsz']): return s['rp']+(rva-s['va'])
    return None

print('=== 0x4da780..0x4da808 数据（找表边界）===')
o=r2o(0x4da780-IMG)
for k in range(0, 0x88, 4):
    a=0x4da780+k
    v=u32(o+k)
    tag=''
    if 0x400000<v<0x500000: tag=' <code/data>'
    print('  0x%06X: 0x%08X%s'%(a,v,tag))

print()
print('=== .text 中引用 0x4da7xx 的指令 ===')
blob=d[T['rp']:T['rp']+T['rsz']]
BASE=IMG+T['va']
md=Cs(CS_ARCH_X86, CS_MODE_32)
hits=0
for i in range(len(blob)-5):
    # 找 imm32 == 0x4da7c0 / 0x4da7d0 / 0x4da7e0 / 0x4da800
    v=struct.unpack_from('<I',blob,i)[0]
    if v in (0x4da7c0,0x4da7d0,0x4da7e0,0x4da800,0x4da780,0x4da7a0):
        # 反汇编该地址前 8 字节确认是指令边界
        for ins in md.disasm(blob[max(0,i-8):i+6], BASE+max(0,i-8)):
            if ins.address<=BASE+i<ins.address+len(ins.bytes):
                print('  0x%06X  %-20s %s %s'%(ins.address,' '.join('%02X'%b for b in ins.bytes),ins.mnemonic,ins.op_str))
                hits+=1
                break
print('  合计 %d'%hits)
