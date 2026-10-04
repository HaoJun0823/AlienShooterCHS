#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""反汇编 sub_426680（文本块绘制主循环）全文，重点看对齐算式与字段用法。"""
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
                     va=u32(b+12),vsz=u32(b+8),rp=u32(b+20),rsz=u32(b+16)))
T=[s for s in secs if s['name']=='.text'][0]
IMG=0x400000
md=Cs(CS_ARCH_X86, CS_MODE_32)

def dis(va, n):
    rva=va-IMG
    off=T['rp']+(rva-T['va'])
    code=d[off:off+n]
    for ins in md.disasm(code, va):
        bs=' '.join('%02X'%b for b in ins.bytes)
        print('  %08X  %-22s %s %s  (rva 0x%06X)'%(ins.address,bs,ins.mnemonic,ins.op_str,ins.address-IMG))

print('='*100)
print('sub_426680  prologue + 对齐段 (0x426680-0x426880, 0x200 字节)')
print('='*100)
dis(0x426680, 0x200)

print()
print('='*100)
print('循环体关键段 (0x426860-0x426A80)')
print('='*100)
dis(0x426860, 0x220)
