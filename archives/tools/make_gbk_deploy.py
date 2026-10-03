# -*- coding: utf-8 -*-
"""
make_gbk_deploy.py —— 把 data_chs（UTF-8 BOM 中文）转成 GB18030 放到 deploy_gbk/

为什么需要这一份：
    Alien Shooter 的引擎按「单字节流」读取文本并直接喂给 GDI 的
    ExtTextOutA。Win32 的 *A 版 API 按系统 ANSI 代码页解释字节，
    中文 Windows 下即 CP936/GBK。所以磁盘上的文本必须是 GBK，
    而不是 UTF-8 —— UTF-8 的中文是 3 字节序列，喂进 ANSI 管线会变成
    一堆乱码废字（锟斤拷）。

产出：
    deploy_gbk/Text/*.txt      71 个剧情/菜单文本
    deploy_gbk/strings.ini     界面字符串

行尾处理：
    保留原始 CRLF。引擎靠 \r\n 分行，改成 LF 会丢换行。
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "data_chs")
DST = os.path.join(ROOT, "deploy_gbk")
# GB18030 是 GBK 的超集，能覆盖更多汉字；Windows 的 CP936 实际实现就是 GB18030。
TARGET_ENC = "gb18030"


def convert(src_path, dst_path):
    with open(src_path, "rb") as f:
        raw = f.read()

    # 去掉 UTF-8 BOM（GBK 文本不需要，且引擎可能把 EF BB BF 当正文）
    if raw[:3] == b"\xEF\xBB\xBF":
        raw = raw[3:]

    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as e:
        print("  [SKIP] UTF-8 解码失败: %s (%s)" % (src_path, e))
        return False

    # 规范化统一转回 CRLF
    text = text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")

    try:
        out = text.encode(TARGET_ENC)
    except UnicodeEncodeError as e:
        print("  [FAIL] %s 编码失败: %s" % (TARGET_ENC, e))
        return False

    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, "wb") as f:
        f.write(out)
    return True


def main():
    if not os.path.isdir(SRC):
        print("找不到源目录: %s" % SRC)
        return 1

    n_ok = n_fail = 0
    for dirpath, _, filenames in os.walk(SRC):
        rel = os.path.relpath(dirpath, SRC)
        if rel == ".":
            rel = ""
        for fn in sorted(filenames):
            if not fn.lower().endswith((".txt", ".ini")):
                continue
            src_path = os.path.join(dirpath, fn)
            # TERMS.md / README.md 之类不参与部署
            if fn.lower().endswith(".md"):
                continue
            dst_path = os.path.join(DST, rel, fn) if rel else os.path.join(DST, fn)
            if convert(src_path, dst_path):
                n_ok += 1
            else:
                n_fail += 1

    print("转换完成: 成功 %d, 失败 %d -> %s" % (n_ok, n_fail, DST))
    return 0 if n_fail == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
