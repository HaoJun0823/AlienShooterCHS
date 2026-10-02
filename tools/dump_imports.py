# -*- coding: utf-8 -*-
"""
dump_imports.py -- 解析 PE 导入表（含延迟导入、绑定导入、IAT RVA）

用法:
    python dump_imports.py <exe路径> [--md 输出.md] [--raw]

只读操作，不修改目标文件。
"""
import sys
import os
import argparse

try:
    import pefile
except ImportError:
    sys.exit("需要 pefile: pip install pefile")


def fmt_flags(flags, mapping):
    out = [name for bit, name in mapping if flags & bit]
    return "|".join(out) if out else "0"


DLL_CHARS = [
    (0x0020, "HIGH_ENTROPY_VA"),
    (0x0040, "DYNAMIC_BASE(ASLR)"),
    (0x0080, "FORCE_INTEGRITY"),
    (0x0100, "NX_COMPAT(DEP)"),
    (0x0200, "NO_ISOLATION"),
    (0x0400, "NO_SEH"),
    (0x0800, "NO_BIND"),
    (0x1000, "APPCONTAINER"),
    (0x2000, "WDM_DRIVER"),
    (0x4000, "GUARD_CF"),
    (0x8000, "TERMINAL_SERVER_AWARE"),
]


def dump_imports(pe, raw=False):
    lines = []
    total = 0

    # ---- 常规导入表 ----
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode("ascii", "replace")
            imp = entry.imports
            n_ord = sum(1 for i in imp if i.ordinal and not i.name)
            lines.append((dll, len(imp), n_ord))
            total += len(imp)
            if raw:
                for i in imp:
                    if i.ordinal and not i.name:
                        name = "Ordinal#%d" % i.ordinal
                    else:
                        name = i.name.decode("ascii", "replace") if i.name else "?"
                    lines.append(("    %-52s hint=%-6s iat=0x%08X"
                                  % (name, i.hint if hasattr(i, "hint") and i.hint is not None else "-",
                                     i.address),))
    else:
        lines.append(("<无导入表>", 0, 0))

    print("=" * 78)
    print("导入表 (IMAGE_IMPORT_DESCRIPTOR)")
    print("=" * 78)
    print("%-34s %6s %6s" % ("DLL", "函数数", "Ordinal"))
    print("-" * 78)
    total = 0
    for row in lines:
        if raw and row[0].startswith("    "):
            print(row[0])
            continue
        if len(row) == 3:
            print("%-34s %6d %6d" % row)
            total += row[1]
        else:
            print(row[0])
    print("-" * 78)
    print("DLL 总数: %d   导入函数总数: %d" % (
        len([r for r in lines if len(r) == 3 and r[0] != "<无导入表>"]), total))

    # ---- 逐函数明细 ----
    print()
    print("=" * 78)
    print("导入函数明细")
    print("=" * 78)
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        dll = entry.dll.decode("ascii", "replace")
        print("[%s]" % dll)
        for i in entry.imports:
            if i.ordinal and not i.name:
                nm = "Ordinal#%d" % i.ordinal
            else:
                nm = i.name.decode("ascii", "replace") if i.name else "?"
            print("    0x%08X  %s" % (i.address, nm))
        print()

    # ---- 延迟导入 ----
    delay = getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", None)
    print("=" * 78)
    print("延迟导入表 (Delay Import)")
    print("=" * 78)
    if delay:
        for entry in delay:
            dll = entry.dll.decode("ascii", "replace")
            print("[%s]  %d 个函数" % (dll, len(entry.imports)))
            for i in entry.imports:
                if i.ordinal and not i.name:
                    nm = "Ordinal#%d" % i.ordinal
                else:
                    nm = i.name.decode("ascii", "replace") if i.name else "?"
                print("    0x%08X  %s" % (i.address, nm))
            print()
    else:
        print("(无)")

    # ---- 绑定导入 ----
    bound = getattr(pe, "DIRECTORY_ENTRY_BOUND_IMPORT", None)
    print("=" * 78)
    print("绑定导入 (Bound Import)")
    print("=" * 78)
    if bound:
        for entry in bound:
            print("  %s  TimeDateStamp=0x%08X  forwarder=%s"
                  % (entry.name.decode("ascii", "replace"), entry.struct.TimeDateStamp,
                     entry.struct.ForwarderRefs))
    else:
        print("(无)")

    # ---- 附加信息 ----
    print()
    print("=" * 78)
    print("PE 概要")
    print("=" * 78)
    print("Machine          : 0x%04X (%s)" % (
        pe.FILE_HEADER.Machine,
        "x86" if pe.FILE_HEADER.Machine == 0x14C else
        "x64" if pe.FILE_HEADER.Machine == 0x8664 else "?"))
    print("ImageBase        : 0x%08X" % pe.OPTIONAL_HEADER.ImageBase)
    print("EntryPoint(RVA)  : 0x%08X" % pe.OPTIONAL_HEADER.AddressOfEntryPoint)
    print("Subsystem        : %d (%s)" % (
        pe.OPTIONAL_HEADER.Subsystem,
        {1: "NATIVE", 2: "WINDOWS_GUI", 3: "WINDOWS_CUI"}.get(pe.OPTIONAL_HEADER.Subsystem, "?")))
    print("DllCharacteristics: 0x%04X [%s]" % (
        pe.OPTIONAL_HEADER.DllCharacteristics,
        fmt_flags(pe.OPTIONAL_HEADER.DllCharacteristics, DLL_CHARS)))
    imp_rva = pe.OPTIONAL_HEADER.DATA_DIRECTORY[1].VirtualAddress
    imp_sz = pe.OPTIONAL_HEADER.DATA_DIRECTORY[1].Size
    print("Import Directory : RVA=0x%08X  Size=0x%X" % (imp_rva, imp_sz))
    print("链接器版本        : %d.%d" % (pe.OPTIONAL_HEADER.MajorLinkerVersion,
                                        pe.OPTIONAL_HEADER.MinorLinkerVersion))
    ts = pe.FILE_HEADER.TimeDateStamp
    import datetime
    print("TimeDateStamp    : 0x%08X (%s)" % (
        ts, datetime.datetime.utcfromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S UTC")))
    print("文件大小         : %d 字节" % len(pe.__data__))

    # 校验和
    cs = pe.OPTIONAL_HEADER.CheckSum
    calc = pe.generate_checksum()
    print("CheckSum         : 0x%08X (计算值 0x%08X, %s)" % (
        cs, calc, "一致" if cs == calc else "不一致"))

    # 资源/子系统里的额外 DLL 引用（如 .rsrc 版本信息）
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("--raw", action="store_true", help="在概览中显示每条函数")
    args = ap.parse_args()

    path = args.exe
    if not os.path.isfile(path):
        sys.exit("文件不存在: %s" % path)

    pe = pefile.PE(path, fast_load=False)
    try:
        pe.parse_data_directories()
    except Exception:
        pass
    dump_imports(pe, raw=args.raw)
    pe.close()


if __name__ == "__main__":
    main()