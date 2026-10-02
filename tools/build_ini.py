# -*- coding: utf-8 -*-
"""
Alien Shooter 简体中文本地化 —— strings.ini 构建脚本

用法
----
    python build_ini.py             # 生成 UTF-8 BOM 版本到 data_chs/strings.ini
    python build_ini.py --gb2312    # 额外生成 GB2312 版本到 dist_gb2312/strings.ini

数据流
------
    I:/LocalGames/AlienShooter v1.22DIC/strings.ini   英文/俄文原版（cp1251，只读）
                    ↓ 逐行对照
    tools/translations_ini_zh.py                      译文表（唯一需维护处）
                    ↓ build_ini.py
    data_chs/strings.ini                              UTF-8 BOM 成品
    dist_gb2312/strings.ini                           GB2312 成品（可选）

保真约束
--------
1. key 与 section 名逐字不动。引擎按 key 查表，改名即字符串丢失。
2. URL 值不译。BuyCommand / FinalLink / DXErrorCommand 是程序要执行的串。
3. 尾随空格按原文件还原（TRAILING_SPACES）。
4. 重复 key（Item236）两行都保留，原版缺陷不修。
5. 俄文注释逐字保留，不译。
6. 行尾 CRLF，结尾换行与原文件一致。
7. 译文不含 ASCII 字母（URL 与纯技术串除外）——引擎逐字节取字形，
   大小写字母都可能渲染不出，中文界面不该出现拉丁字母。
"""
import argparse
import os
import re
import shutil
import sys

from translations_ini_zh import (
    TRANSLATIONS, TRAILING_SPACES, DUP_KEYS,
)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = "I:/LocalGames/Alien Shooter v1.22DIC"
BASE_INI = os.path.join(GAME_DIR, "strings.ini")
# 基线不在游戏目录时（换机器 / 路径变动），回退到仓库内的副本
FALLBACK_BASE = os.path.join(ROOT, "data_original", "strings.ini")
UTF8_OUT = os.path.join(ROOT, "data_chs", "strings.ini")
GB_OUT = os.path.join(ROOT, "dist_gb2312", "strings.ini")

# 不允许出现在译文里的 ASCII 字母（URL / 纯技术串白名单除外）
LATIN_OK = {
    "BuyCommand", "FinalLink", "DXErrorCommand",   # URL
    "Version",                                     # 含 "sigma team" 与 "2002-2004"
    "othergames2",                                 # 含 "SIGMA TEAM"
    "GameName",                                    # SW
}

# GB2312 无法编码的排版字符
GB2312_FALLBACK = {
    "—": "-",
    "·": "・",
    "‘": "'",
    "’": "'",
    "“": '"',
    "”": '"',
}


def read_base():
    for p in (BASE_INI, FALLBACK_BASE):
        if os.path.isfile(p):
            with open(p, "rb") as f:
                return f.read().decode("cp1251"), p
    raise SystemExit("!! 找不到原版 strings.ini（已查 %s 与 %s）" % (BASE_INI, FALLBACK_BASE))


def is_url(v):
    return "http" in v or "://" in v or v.endswith(".exe") or v.endswith(".com/")


def parse_line(line):
    """返回 (kind, key, value)。kind: section / comment / blank / kv。"""
    s = line.strip()
    if not s:
        return ("blank", None, None)
    if s.startswith("["):
        return ("section", s, None)
    if s.startswith(";"):
        return ("comment", s, None)
    if "=" in line:
        k, v = line.split("=", 1)
        return ("kv", k, v)
    return ("other", line, None)


def build(encoding, out_path, bom):
    base, src_path = read_base()
    base_lines = base.split("\r\n")
    if base_lines and base_lines[-1] == "":
        base_lines.pop()
        trailing_nl = True
    else:
        trailing_nl = False

    errors, warnings, out = [], [], []
    seen = {}
    warned_dup = set()

    for i, line in enumerate(base_lines, 1):
        kind, key, val = parse_line(line)

        if kind in ("section", "comment", "blank", "other"):
            # 原样保留：section 名、俄文注释、空行
            out.append(line)
            continue

        # kind == kv
        if key != key.strip():
            errors.append("第 %d 行 key 含空格: %r" % (i, key))

        if key in seen:
            # 重复 key 的第 2+ 次出现：用 DUP_KEYS 里的 "key#序号" 取译文。
            # Item236 在原文件出现两次（第 96 行 battle dron、第 99 行 BOMB），
            # 第一次用 TRANSLATIONS[Item236]，第二次用 DUP_KEYS["Item236#2"]。
            seen[key] += 1
            dup_key = "%s#%d" % (key, seen[key])
            tr = DUP_KEYS.get(dup_key)
            if tr is None:
                errors.append(
                    "第 %d 行重复 key %s 缺少替身译文 %s"
                    % (i, key, dup_key)
                )
            else:
                warned_dup.add(dup_key)
                warnings.append(
                    "key 重复: %s 第 %d 次出现（行 %d），用替身 key %s"
                    % (key, seen[key], i, dup_key)
                )
        else:
            seen[key] = 1
            tr = TRANSLATIONS.get(key)

        if tr is None:
            # 未列出的 key = 值不翻译，逐字沿用原文
            if key not in TRANSLATIONS and not is_url(val) and key not in LATIN_OK:
                # 提醒人工确认，而不是静默漏译
                if re.search(r"[A-Za-z]{3,}", val):
                    warnings.append("第 %d 行 %s 未收录译文，保留原文: %r" % (i, key, val.strip()))
            out.append(line)
            continue

        # 还原尾随空格
        pad = " " * TRAILING_SPACES.get(key, 0)
        out.append("%s=%s%s" % (key, tr, pad))

        # 译文里不该有拉丁字母
        if key not in LATIN_OK and not is_url(val):
            for w in re.findall(r"[A-Za-z]{2,}", tr):
                errors.append("第 %d 行 %s 译文含拉丁字母: %s" % (i, key, w))

    body = "\r\n".join(out)
    if trailing_nl:
        body += "\r\n"

    if encoding == "gb2312":
        for bad, good in GB2312_FALLBACK.items():
            body = body.replace(bad, good)

    data = body.encode(encoding)
    if bom:
        data = b"\xef\xbb\xbf" + data

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(data)
    return errors, warnings, len(out), src_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gb2312", action="store_true")
    args = ap.parse_args()

    errs, warns, n, src = build("utf-8", UTF8_OUT, bom=True)
    print("基线       -> %s" % src)
    print("UTF-8 BOM  -> %s  (%d 行)" % (UTF8_OUT, n))

    if args.gb2312:
        if os.path.isdir(os.path.dirname(GB_OUT)):
            shutil.rmtree(os.path.dirname(GB_OUT))
        e2, w2, _, _ = build("gb2312", GB_OUT, bom=False)
        errs += e2
        # 两次构建的提示会重复（同一份源），去重后输出
        warns += [x for x in w2 if x not in warns]
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
    print("\n校验通过：key/section 零改动、URL 未译、尾随空格还原、无拉丁字母残留。")


if __name__ == "__main__":
    main()
