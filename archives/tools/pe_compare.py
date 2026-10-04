#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""两版 AlienShooter.exe 的 PE 结构对比：节表 / 导入表 / 指纹 / 代码密度。"""
import sys, struct, hashlib

def u16(d,o): return struct.unpack_from('<H',d,o)[0]
def u32(d,o): return struct.unpack_from('<I',d,o)[0]

class PE:
    def __init__(self, path):
        self.path = path
        self.d = d = open(path,'rb').read()
        self.pe = pe = u32(d,0x3C)
        assert d[pe:pe+4]==b'PE\x00\x00', 'not PE'
        self.mach   = u16(d,pe+4)
        self.nsec   = u16(d,pe+6)
        self.stamp  = u32(d,pe+8)
        self.opt    = opt = pe+24
        self.magic  = u16(d,opt)
        self.linker = (d[opt+2], d[opt+3])
        self.imagebase = u32(d,opt+28)
        self.secalign  = u32(d,opt+32)
        self.filealign = u32(d,opt+36)
        self.sizeofimage = u32(d,opt+56)
        self.entryrva = u32(d,opt+16)
        self.subsys  = u16(d,opt+68)
        self.nddva   = u32(d,opt+92)
        self.sechdr  = opt + u16(d,pe+20)
        self.sections=[]
        for i in range(self.nsec):
            b=self.sechdr+40*i
            name=d[b:b+8].rstrip(b'\x00').decode('latin1')
            vsz=u32(d,b+8); va=u32(d,b+12); rsz=u32(d,b+16); rp=u32(d,b+20)
            ch=u32(d,b+36)
            self.sections.append(dict(name=name,va=va,vsz=vsz,rp=rp,rsz=rsz,ch=ch))

    def rva2off(self, rva):
        for s in self.sections:
            if s['va'] <= rva < s['va']+max(s['vsz'],s['rsz']):
                return s['rp'] + (rva - s['va'])
        return None

    def bytes_at_rva(self, rva, n):
        o=self.rva2off(rva)
        return None if o is None else self.d[o:o+n]

    def imports(self):
        d=self.d
        res={}
        imp_rva = u32(d, self.opt+96+8)   # Import Directory (offset 96 in opt for PE32 = 0x60)
        if self.magic==0x10b: imp_rva = u32(d,self.opt+104)
        else: imp_rva = u32(d,self.opt+120)
        off = self.rva2off(imp_rva)
        if off is None: return res
        i=0
        while True:
            o = off + 20*i
            if o+20 > len(d): break
            oft=u32(d,o); ts=u32(d,o+4); fc=u32(d,o+8); nm=u32(d,o+12); ft=u32(d,o+16)
            if nm==0 and ft==0: break
            no=self.rva2off(nm)
            dll = d[no:d.index(b'\x00',no)].decode('latin1') if no else '?'
            # 枚举该 DLL 的所有导入函数（含 IAT 槽 RVA）
            funcs=[]
            to = self.rva2off(oft if oft else ft)
            slot_rva = ft
            j=0
            while to is not None:
                e = u32(d,to+4*j)
                if e==0: break
                if e & 0x80000000:
                    funcs.append(('#%d'%e, slot_rva+4*j))
                else:
                    fo=self.rva2off(e)
                    if fo is None: break
                    end=d.index(b'\x00',fo+2)
                    funcs.append((d[fo+2:end].decode('latin1'), slot_rva+4*j))
                j+=1
            res[dll]=funcs
            i+=1
        return res

    def entropy(self, data):
        if not data: return 0.0
        from collections import Counter
        c=Counter(data); n=len(data)
        import math
        return -sum(v/n*math.log2(v/n) for v in c.values())

def dump(p):
    pe=PE(p)
    print('='*78)
    print(p)
    print('  Machine=0x%04X  Sections=%d  TimeDateStamp=0x%08X (%s)' % (
        pe.mach, pe.nsec, pe.stamp,
        __import__('time').strftime('%Y-%m-%d %H:%M:%S', __import__('time').gmtime(pe.stamp))))
    print('  Magic=0x%X  Linker=%d.%d' % (pe.magic, pe.linker[0], pe.linker[1]))
    print('  ImageBase=0x%08X  EntryPoint RVA=0x%08X  SizeOfImage=0x%08X  Subsystem=%d' % (
        pe.imagebase, pe.entryrva, pe.sizeofimage, pe.subsys))
    print('  SectionAlign=0x%X  FileAlign=0x%X  DataDirs=%d' % (pe.secalign, pe.filealign, pe.nddva))
    print('  --- sections ---')
    print('    %-9s %-9s %-9s %-9s %-8s %s' % ('name','VA','VSZ','RP','RSZ','chars'))
    for s in pe.sections:
        body = pe.d[s['rp']:s['rp']+s['rsz']] if s['rp'] else b''
        ent = pe.entropy(body)
        print('    %-9s %08X %08X %08X %08X %08X  H=%.3f' % (
            s['name'],s['va'],s['vsz'],s['rp'],s['rsz'],s['ch'],ent))
    imps = pe.imports()
    print('  --- imports (%d DLL) ---' % len(imps))
    for dll in sorted(imps):
        fns = imps[dll]
        print('    %-22s %3d  %s' % (dll, len(fns), ', '.join(f for f,_ in fns[:8]) + (' ...' if len(fns)>8 else '')))
    return pe

def cmp_hooks(peA, peB):
    """在两版里按现有 hook 点的 RVA 找对应字节，判断代码是否同源。"""
    print()
    print('='*78)
    print('按 v1.22DIC 已知 RVA 在两版 .text 内做字节块搜索（判断是否同一函数）')
    d1=peA.d; d2=peB.d
    # 取 DIC .text 全块
    def text(pe):
        for s in pe.sections:
            if s['name']=='.text':
                return s['rp'], s['rsz'], s['va']
        return None
    t1=text(peA); t2=text(peB)
    if not t1 or not t2:
        print('  ! .text 未找到'); return
    rp1,rs1,_=t1; rp2,rs2,_=t2
    blob1=d1[rp1:rp1+rs1]; blob2=d2[rp2:rp2+rs2]
    def find(blob,pat):
        out=[];i=0
        while True:
            j=blob.find(pat,i)
            if j<0: break
            out.append(j); i=j+1
        return out
    return blob1, blob2, find

if __name__=='__main__':
    a=dump(sys.argv[1])
    print()
    b=dump(sys.argv[2])
