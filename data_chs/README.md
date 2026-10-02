# Alien Shooter 简体中文本地化

《孤胆枪手》(Alien Shooter v1.22DIC) 的中文本地化工具链。

## 现状

⚠️ **文本已全部翻译，但游戏内还显示不出中文。**

按逆向结论（见 `.workbuddy/memory/2026-10-02.md`），引擎 `sub_426680` **逐字节**处理
文本、`PICTURE_FONT` 容量 **256 写死**、`001font.tga` **仅 152 字形且无 CJK**。
必须先落地方向 A 的 DLL hook（UTF-8 → 字形索引 + CJK 字形图集），中文才能上屏。

## 目录结构

```
data_original/          只读基线，绝不修改
  Text/*.txt            71 个关卡/剧情/武器/道具文本（英文，从 git 恢复）
  strings.ini           110 个界面/物品 key（cp1251，从游戏目录取得）

data_chs/               成品（UTF-8 BOM，主产物）
  Text/*.txt            71 个中文文本
  strings.ini           中文界面文本
  TERMS.md              术语表，全表一致性唯一依据

dist_gb2312/            成品（GB2312，备用编码）
  Text/*.txt
  strings.ini

tools/
  translations_zh.py    Text/ 译文表（唯一需维护处）
  translations_ini_zh.py strings.ini 译文表
  build_chs.py          Text/ 构建脚本
  build_ini.py          strings.ini 构建脚本
  build_all.py          一键构建两个产物
```

## 用法

```bash
cd tools
python build_all.py            # 只出 UTF-8 BOM
python build_all.py --gb2312   # 同时出 GB2312
```

单独构建：

```bash
python build_chs.py [--gb2312]   # 只重建 Text/
python build_ini.py [--gb2312]   # 只重建 strings.ini
```

改译文只改 `translations_zh.py` / `translations_ini_zh.py`，然后重跑构建。
**永远不要手改 `data_chs/` 下的成品**，会被下次构建覆盖。

## 为什么先出 UTF-8 BOM

Windows 记事本 / VS / 现代编辑器都靠 BOM 猜编码，缺 BOM 的 UTF-8 会被当成 ANSI
打开，中文变乱码。等 hook 摸清游戏的读取逻辑后，用 `--gb2312` 出对应版本即可。

## 构建期强校验

任一项失败即整体失败，不会产出半成品：

**Text/（`build_chs.py`）**
- 分区标记（任务简报/提示/目标/参数/描述）数量必须与英文基线一一对齐
  —— UI 按标记分区渲染，丢一个标记就少一块
- 行数不得超过英文基线（中文更紧凑，变少是安全的，底部留白无害）
- 单行显示宽度 ≤ 28 字（源文本是硬换行定宽，引擎不自动换行）
- 行尾 CRLF，无制表符
- 结尾有无换行与基线一致

**strings.ini（`build_ini.py`）**
- **key 与 section 名逐字不改，序列完全一致** —— 引擎按 key 查表，改名即字符串丢失
- URL 一律不译（`BuyCommand` / `FinalLink` / `DXErrorCommand` 是程序要执行的串）
- 尾随空格按原文件精确还原（`Low` / `Item304` / `Item305` / `Item308` / `Item309`）
- 重复 key（`Item236`）保留两行，不修原版缺陷
- **译文不得含拉丁字母** —— 引擎逐字节取字形，大小写字母都可能渲染不出

## 已知坑

1. **`.gitattributes` 的 `* text=auto` 会把 CRLF 归一化成 LF**，游戏读到裸 LF 会出问题。
   已为 `data_chs/Text/*.txt`、`data_chs/strings.ini` 加 `-text` 锁定。
2. **GB2312 装不下 `——`(U+2014) 和 `·`(U+00B7)**，构建脚本有 `GB2312_FALLBACK`
   降级表，直接编码会抛 `UnicodeEncodeError`。
3. **构建脚本不能原地覆盖基线目录**，否则校验器拿自己的输出当基线，自我比对全绿。
   基线一律从 `data_original/` 读。
4. **从 git 恢复的文件是 LF**，需转回 CRLF 才能正确比对。
5. **`data_original/strings.ini` 含俄文 cp1251 字节，git 会当二进制**，
   必要时 `git add -f`。
