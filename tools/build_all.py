# -*- coding: utf-8 -*-
"""
Alien Shooter 简体中文本地化 —— 一键构建入口

用法
----
    python build_all.py            # 只出 UTF-8 BOM 版
    python build_all.py --gb2312   # 同时出 GB2312 版

等价于依次执行 build_chs.py 与 build_ini.py，但只汇总一份报告。
任一环节失败即整体失败（非零退出码）。
"""
import subprocess
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
STEPS = ["build_chs.py", "build_ini.py"]


def main():
    args = [a for a in sys.argv[1:]]
    failed = []
    for step in STEPS:
        print("=" * 60)
        print(">> %s" % step)
        print("=" * 60)
        r = subprocess.run([sys.executable, os.path.join(HERE, step)] + args)
        if r.returncode != 0:
            failed.append(step)
        print()

    if failed:
        print("构建失败: %s" % ", ".join(failed))
        sys.exit(1)
    print("全部构建完成。")


if __name__ == "__main__":
    main()
