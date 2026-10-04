#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
AOB 可行性实测（对齐 aob-xversion-hook-port Step 1/2/3）：
1) 从 DIC 版 exe 取出各 hook 点的真实字节，构造 L1 特征码
2) 用通配扫描在【两版 exe 磁盘文件】和【原版运行时可执行段】上找命中
3) 报告命中数 -> 直接回答"能不能改成 AOB"
"""
import sys, struct, math
from collections import Counter

def u16(d,o): return struct.unpack_from('<H',d,o)[0]
def u32(d,o): return struct.unpack_from('<I',d,o)[0]

class PE:
    def __init__(self,path):
        self.path=path; self.d=d=open(path,'rb').read()
        self.pe=u32(d,0x3C); self.nsec=u16(d,self.pe+6)
        self.opt=self.pe+24; self.imagebase=u32(d,self.opt+28)
        self.entry=u32(d,self.opt+16)
        self.sec=self.opt+u16(d,self.pe+20)
        self.sections=[]
        for i in range(self.nsec):
            b=self.sec+40*i
            self.sections.append(dict(
                name=d[b:b+8].rstrip(b'\x00').decode('latin1'),
                va=u32(d,b+12), vsz=u32(d,b+8), rp=u32(d,b+20), rsz=u32(d,b+16),
                ch=u32(d,b+36)))
    def r2o(self,rva):
        for s in self.sections:
            if s['va']<=rva<s['va']+max(s['vsz'],s['rsz']):
                return s['rp']+(rva-s['va'])
        return None
    def code_blob(self):
        """把所有可执行段拼起来（用于扫描），返回 (blob, [(va,len)..])"""
        parts=[];segs=[]
        for s in self.sections:
            if (s['ch'] & 0x20000000) and s['rsz']:   # MEM_EXECUTE
                parts.append(self.d[s['rp']:s['rp']+s['rsz']])
                segs.append((s['va'],s['rsz'],b''.join(parts) and sum(len(p) for p in parts[:-1])))
        return b''.join(parts), segs
    def rva_to_bloboff(self,rva,segs):
        for va,ln,base in segs:
            if va<=rva<va+ln: return base+(rva-va)
        return None

def mask_wildcards(bs, rules):
    """按规则把易漂移的操作数置为通配。返回 (sig, mask01)。"""
    m=[1]*len(bs)
    for off,ln in rules:
        for k in range(off,min(off+ln,len(m))): m[k]=0
    return bs,m

def scan(blob, sig, mask, limit=None):
    """通配扫描：返回命中数与 blob 内偏移列表"""
    n=len(sig); hits=[]
    # 锚点 = 第一个非通配字节
    anchor=None
    for i,v in enumerate(mask):
        if v: anchor=i; break
    if anchor is None: return hits
    a=sig[anchor]
    i=0
    while True:
        j=blob.find(bytes([a]),i)
        if j<0: break
        if j+ n<=len(blob):
            ok=True
            for k in range(n):
                if mask[k] and blob[j+k]!=sig[k]: ok=False; break
            if ok:
                hits.append(j)
                if limit and len(hits)>=limit: return hits
        i=j+1
    return hits

def fmt(bs,mask,marks=None):
    out=[]
    for i,(b,mk) in enumerate(zip(bs,mask)):
        mk = mk if isinstance(mk,int) else int(mk)
        s = ('%02X'%b) if mk else '??'
        if marks and i in marks: s='[%s]'%s
        out.append(s)
    return ' '.join(out)

DIC='I:/LocalGames/AlienShooter v1.22DIC/AlienShooter.exe'
OLD='I:/LocalGames/AlienShootershuang/AlienShooter.exe'

HOOKS = [
    # name,                RVA,      len, 通配区间[(off,len)...]
    ('DrawText prologue',  0x26680,  16, [(5,4)]),          # 68 <SEH imm32>
    ('DrawText 续',          0x2668A,  16, []),
    ('FrameEnd patch',     0x3041E,   6, []),               # 8B 87 28 0E 00 00
    ('IAT Direct3DCreate9',0xD9314,   6, [(0,4)]),           # IAT 槽：全通配对比无意义
]

def main():
    dic=PE(DIC); old=PE(OLD)
    db,ds=dic.code_blob()
    ob,os_=old.code_blob()
    print('DIC  可执行段合计 %d 字节  段数 %d'%(len(db),len(ds)))
    print('原版 可执行段合计 %d 字节  段数 %d'%(len(ob),len(os_)))
    print()
    print('%-24s %-10s %-10s %s'%('HOOK','DIC命中','原版命中','特征码'))
    print('-'*100)
    for name,rva,ln,wild in HOOKS:
        off=dic.rva_to_bloboff(rva,ds)
        if off is None:
            print('%-24s %-10s %-10s (RVA 不在可执行段)'%(name,'-','-')); continue
        bs=db[off:off+ln]
        sig,mask=mask_wildcards(bs,wild)
        hd=scan(db,sig,mask)
        ho=scan(ob,sig,mask)
        print('%-24s %-10d %-10d %s'%(name,len(hd),len(ho),fmt(bs,mask)))
    print()

    # 反向：原版自身的 code 能否与 DIC 有任何公共子串（粗略同源判定）
    print('=== 同源性粗判：两版 .text 的公共 16 字节片段数 ===')
    def ngrams(b,n=16,step=64):
        s=set()
        for i in range(0,len(b)-n,step):
            s.add(b[i:i+n])
        return s
    ga=ngrams(db); gb=ngrams(ob)
    # 只统计"有意义"的（排除全 00/FF）
    ga={x for x in ga if len(set(x))>6}
    gb={x for x in gb if len(set(x))>6}
    inter=ga&gb
    print('  DIC 片段数 %d   原版片段数 %d   交集 %d'%(len(ga),len(gb),len(inter)))
    if inter:
        for x in list(inter)[:10]:
            print('   ', ' '.join('%02X'%b for b in x))

if __name__=='__main__':
    main()
