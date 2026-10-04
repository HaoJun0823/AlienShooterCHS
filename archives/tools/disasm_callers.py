#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""反汇编 sub_4306A0 的调用点上下文，并回溯所属函数序言。"""
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
import capstone

path='I:/LocalGames/AlienShooter v1.22DIC/AlienShooter.exe'
d=open(path,'rb').read()
def u16(o): return struct.unpack_from('<H',d,o)[0]
def u32(o): return struct.unpack_from('<I',d,o)[0]
pe=u32(0x3C); nsec=u16(pe+6); opt=pe+24
sec=opt+u16(pe+20)
secs=[]
for i in range(nsec):
    b=sec+40*i
    secs.append(dict(name=d[b:b+8].rstrip(b'\x00').decode('latin1'),
                     va=u32(b+12),vsz=u32(b+8),rp=u32(b+20),rsz=u32(b+16),ch=u32(b+36)))
TEXT=[s for s in secs if s['name']=='.text'][0]
TBASE=TEXT['va']; TRP=TEXT['rp']; TRSZ=TEXT['rsz']

md=Cs(CS_ARCH_X86, CS_MODE_32)
md.detail=True

IMG=0x400000
def r2o(rva):
    if rva < TBASE or rva >= TBASE+TRSZ: return None
    return TRP + (rva - TBASE)

def dis(va, n=40, back=0x40):
    """从 va-back 处开始反汇编，标出 va 位置（va 为 VA）"""
    start=va-back
    off=r2o(start-IMG)
    if off is None: return ['  <越界>']
    code=d[off:off+back+n*8]
    out=[]
    for ins in md.disasm(code, start):
        mark=' >>>' if ins.address==va else '    '
        out.append('%s %08X  %-24s %s %s'%(mark,ins.address,
                   ' '.join('%02X'%b for b in ins.bytes), ins.mnemonic, ins.op_str))
    return out

def find_prologue(va, maxback=0x800):
    """往前找函数序言 55 8B EC"""
    for k in range(0,maxback):
        off=r2o(va-k-IMG)
        if off is None: break
        if d[off]==0x55 and d[off+1]==0x8B and d[off+2]==0xEC:
            return va-k
    return None

CALLERS=[0x422357,0x427A78,0x429000,0x42BEF8,0x46CFDA,0x46D569]
for c in CALLERS:
    fn=find_prologue(c)
    print('='*90)
    print('call site 0x%06X   所属函数序言 0x%s'%(c, ('%06X'%fn) if fn else '?'))
    for line in dis(c, n=6, back=0x30):
        print('  ',line)
    print()
