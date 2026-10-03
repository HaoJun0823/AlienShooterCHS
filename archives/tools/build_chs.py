# -*- coding: utf-8 -*-
"""
Alien Shooter 简体中文本地化构建脚本

用法
----
    python build_chs.py            # 生成 UTF-8 BOM 版本到 data_chs/Text/
    python build_chs.py --gb2312   # 额外生成 GB2312 版本到 dist_gb2312/Text/

为什么先出 UTF-8 BOM
--------------------
Windows 记事本 / VS / 现代编辑器都靠 BOM 猜编码，缺 BOM 的 UTF-8 会被当成
ANSI 打开，中文变乱码。BOM 是最省事的保险。等 DLL hook 摸清游戏的读取逻辑后，
再用 --gb2312 出对应版本。

数据流
------
    data_original/Text/*.txt   英文基线（只读，从 git 恢复，勿改）
              ↓ 逐行对照
    tools/translations_zh.py   译文数据表（唯一需要维护的地方）
              ↓ build_chs.py
    data_chs/Text/*.txt        UTF-8 BOM 成品
    dist_gb2312/Text/*.txt     GB2312 成品（可选）

保真约束
--------
1. 行尾一律 CRLF。原文件全是 CRLF，游戏按行读。
2. 结尾有无换行与英文基线一致（见 translations_zh.END_NL）。
3. 段落标记数量必须与英文基线一致。UI 按标记分区渲染，丢一个标记就少一块。
   译文标记：任务简报 / 提示 / 目标 / 参数 / 描述
4. 译文行数 <= 英文行数。中文信息密度高于英文，行数变少是正常的（底部留白
   无害）；行数变多会撑破文本框，那才是 bug。
5. 单行显示宽度不超限，不做自动重排。
"""
import argparse
import os
import re
import shutil
import sys

from translations_zh import TRANSLATIONS, END_NL, EMPTY

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, "data_original", "Text")   # 英文基线，只读
UTF8_OUT = os.path.join(ROOT, "data_chs", "Text")
GB_OUT = os.path.join(ROOT, "dist_gb2312", "Text")

# 译文里表示「分区」的标记 -> 必须与英文基线的分区数一一对应
SECTION_TAGS = ["任务简报", "提示", "目标", "参数", "描述", "任务简报:"]
# 英文基线里的分区标记
EN_SECTIONS = [
    r"^MISSION\s", r"^DESCRIPTION", r"^TIPS", r"^OBJECTIVE", r"^PARAMETERS",
]

# 单行显示宽度上限。英文原文按 ~28 半角字符折行，中文按 28 字宽折行更安全
# （中文全角在等宽字体里占 2 格，但本作字体宽度未知，先保守）。
MAX_WIDTH = 28


def read_lines(path):
    """按 CRLF 拆行，去掉末尾空串。空文件返回 []。"""
    with open(path, "rb") as f:
        raw = f.read()
    if not raw:
        return []
    text = raw.decode("utf-8", errors="replace")
    lines = text.split("\r\n")
    if lines and lines[-1] == "":
        lines.pop()
    return lines


def en_sections(lines):
    out = []
    for ln in lines:
        s = ln.strip()
        if not s:
            continue
        for pat in EN_SECTIONS:
            if re.match(pat, s):
                out.append(pat)
                break
    return out


def zh_sections(lines):
    """统计译文里的分区标记。'任务 XX:' 开头算任务标题，不计入分区。"""
    out = []
    for i, ln in enumerate(lines):
        s = ln.strip()
        if not s:
            continue
        if s in ("任务简报:", "提示:", "目标:", "参数:", "描述:"):
            out.append(s)
        elif re.match(r"^任务\s*\d+[:：]", s):
            out.append(r"^MISSION\s")
    return out


# 校验参照文件。addon_level_06 的基线是俄语（cp1251，别人做的翻译），
# 结构不对应任何英文文件，改用英文 addon_level_05 作参照。
BASELINE = {"addon_level_06.txt": "addon_level_05.txt"}


def check(name, lines):
    """结构体检。返回 (errors, warnings)。"""
    err, warn = [], []
    src = read_lines(os.path.join(BASE, BASELINE.get(name, name)))

    # 3. 分区标记必须对齐
    a, b = en_sections(src), zh_sections(lines)
    if len(a) != len(b):
        err.append("分区数 %d != 英文 %d (%s vs %s)" % (len(b), len(a), b, a))

    # 4. 行数不得超过英文
    if len(lines) > len(src):
        err.append("行数 %d > 英文 %d，会撑破文本框" % (len(lines), len(src)))
    elif len(lines) < len(src):
        warn.append("行数 %d < 英文 %d（中文更紧凑，正常）" % (len(lines), len(src)))

    # 5. 行宽
    for i, ln in enumerate(lines):
        if len(ln) > MAX_WIDTH:
            err.append("第 %d 行超宽 %d>%d: %s" % (i + 1, len(ln), MAX_WIDTH, ln))
        if "\t" in ln:
            err.append("第 %d 行含制表符" % (i + 1))
        if ln != ln.rstrip() and ln.strip():
            warn.append("第 %d 行有行尾空格: %r" % (i + 1, ln))

    return err, warn


# GB2312 无法编码的排版字符 -> 等价替换。
# U+2014 EM DASH（——）是中文破折号，但 GB2312 字符集里没有，
# 强行编码会抛 UnicodeEncodeError。降级成中文书名号式的双连字符，
# 视觉差异极小，且 GB2312 / GBK / UTF-8 三种编码下都能正常显示。
GB2312_FALLBACK = {
    "—": "-",     # —— 破折号
    "‑": "-",
    "‘": "'",          # 单引号
    "’": "'",
    "“": '"',          # 双引号
    "”": '"',
    "·": "・",   # 姓名间隔号（credits.txt）
}


def to_gb2312(text):
    """把文本转成 GB2312 可编码的形式。无法映射的字符直接报错。"""
    for bad, good in GB2312_FALLBACK.items():
        text = text.replace(bad, good)
    return text


def build(encoding, outdir, bom):
    if not os.path.isdir(outdir):
        os.makedirs(outdir)
    errors, warnings = [], []
    for name in sorted(TRANSLATIONS):
        lines = TRANSLATIONS[name]
        e, w = check(name, lines)
        errors += ["%s: %s" % (name, x) for x in e]
        warnings += ["%s: %s" % (name, x) for x in w]

        body = "\r\n".join(lines)
        if name not in END_NL:
            body += "\r\n"
        if encoding == "gb2312":
            body = to_gb2312(body)
        data = body.encode(encoding)
        if bom:
            data = b"\xef\xbb\xbf" + data
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(data)

    for name in sorted(EMPTY):
        with open(os.path.join(outdir, name), "wb") as f:
            f.write(b"")

    return errors, warnings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gb2312", action="store_true", help="额外生成 GB2312 版本")
    args = ap.parse_args()

    on_disk = {f for f in os.listdir(BASE) if f.lower().endswith(".txt")}
    missing = (set(TRANSLATIONS) | set(EMPTY)) - on_disk
    extra = on_disk - (set(TRANSLATIONS) | set(EMPTY))
    if missing:
        print("!! 英文基线缺失: %s" % ", ".join(sorted(missing)))
    if extra:
        print("!! 英文基线有未翻译文件: %s" % ", ".join(sorted(extra)))

    errs, warns = build("utf-8", UTF8_OUT, bom=True)
    print("UTF-8 BOM  -> %s" % UTF8_OUT)

    if args.gb2312:
        if os.path.isdir(os.path.dirname(GB_OUT)):
            shutil.rmtree(os.path.dirname(GB_OUT))
        e2, w2 = build("gb2312", GB_OUT, bom=False)
        errs += e2
        warns += w2
        print("GB2312     -> %s" % GB_OUT)

    if warns:
        print("\n提示 %d 条:" % len(warns))
        for p in warns:
            print("  ~", p)

    if errs:
        print("\n错误 %d 条:" % len(errs))
        for p in errs:
            print("  !", p)
        sys.exit(1)

    print("\n结构校验通过：分区标记对齐、行宽合规、无撑框风险。")


if __name__ == "__main__":
    main()
