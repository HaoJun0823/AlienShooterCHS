# -*- coding: utf-8 -*-
"""
Alien Shooter 简体中文本地化 —— strings.ini 译文表

文件结构（原文 122 行 / 2889 字节 / cp1251 / CRLF）
-------------------------------------------------
    [strings]   启动器级字符串：游戏名、CD 检查、DirectX 错误、购买链接
    [menu]      主菜单与游戏内 HUD 全部界面文本
    [items]     拾取物品的提示条（Item204..309）

改译名的唯一位置。build_ini.py 负责编码、行尾、结构校验。

硬约束（引擎侧，改动前必读）
----------------------------
1. **key 与 section 名绝对不能动**。引擎按 key 查表，改名等于字符串丢失。
   本表只提供 value 的译文。
2. **URL 一律不译**。BuyCommand / FinalLink / DXErrorCommand 是程序要执行的
   字符串，DXError 里的 URL 更是要显示给玩家的跳转地址。
3. **行尾空格有语义**。原文件多处值带尾随空格（Low / Item304 / Item305 /
   Item308 / Item309），可能是原作者为对齐菜单做的，保留原样。
4. **重复 key 保留**。Item236 在原文件出现两次（第 96 行 BOMB 之前是
   battle dron，第 99 行又是 BOMB），是原版自带缺陷。两行都保留并翻译，
   行为与原版完全一致（INI 解析时后者覆盖前者，实际生效 BOMB）。
5. **俄文注释不译**。用户已确认保留原样。

术语与 data_chs/TERMS.md 严格一致。
"""

# key -> 中文译文。仅列出需要翻译的 value。
# 未列出的 key 表示「值不翻译」（URL、纯数字、ASCII 技术串等）。
TRANSLATIONS = {
    # ---------------- [strings] ----------------
    # GameName=SW 是窗口标题用的缩写（Sigma World），保持原样不译。
    "NotCD": "请将游戏光盘插入光驱",

    # ---------------- [menu] 主菜单 ----------------
    "Lvl": "关卡",
    "LvlDiff": "难度等级:",
    "Hard": "困难",
    "Normal": "普通",
    "Easy": "简单",
    "Addon2": "战役3",
    "Addon": "战役2",
    "Help": "按 F1 查看帮助",
    "Windowed": "窗口模式",
    "Fullscreen": "全屏",
    "Play": "新游戏",
    "Info": "游戏信息",
    "Options": "选项",
    "FameHall": "最高分",
    "Credits": "制作人员",
    "Exit": "退出",
    "SelMode": "请选择游戏模式",
    "SelPlayer": "请选择存档",
    "Campaign": "战役",
    "PlayerName": "玩家名称:",
    "Empty": "空",
    "Cont": "继续游戏",
    "Survive": "生存模式",
    "Name": "名称:",
    "EntName": "请输入名称",
    "MainMenu": "主菜单",

    # ---------------- [menu] 选项设置 ----------------
    "MusicVolume": "音乐音量",
    "SoundVolume": "音效音量",
    "GraphDetail": "画面细节",
    "Screentype": "屏幕模式:",

    # ---------------- [menu] 成绩 / 结算 ----------------
    "Scores": "成绩:",
    "Scor": "得分:",
    "GameOver": "任务失败",          # GAME OVER
    "Money": "金钱:",
    "PlayTime": "本关时间结束",
    "High": "高",
    "Medium": "中",
    "Low": "低",                     # 原值 "LOW " 带尾随空格，构建时还原
    "SoundVol": "音效音量",
    "MusicVol": "音乐音量",
    "Back": "返回",
    "Continue": "继续",
    "SpaceBar": "按空格键继续",
    "AnyKeyForRebirth": "按空格键重生",
    "Gameover": "任务失败",          # GAMEOVER，与 GameOver 同义
    "MissionComplete": "任务完成",
    "LevelComplite": "本关完成!",

    # ---------------- [menu] 存档 ----------------
    "DefaultName": "存档1",          # SAVE1
    "SaveName": "存档",

    # ---------------- [menu] 物品栏 ----------------
    "Eqp": "装备:",
    "Weapon": "武器:",
    "Health": "生命:",
    "Str": "力量:",
    "Speed": "速度:",
    "Acc": "精度:",
    "Time": "关卡时间:",

    # ---------------- [menu] 结算统计 ----------------
    "Kills": "击杀:",
    "FoundMoney": "获得金钱:",
    "FoundSecret": "发现秘密:",

    # ---------------- [menu] 其他 ----------------
    "ContinuePress": "按任意键继续!",
    "othergames1": "其他游戏",
    "othergames2": "来自 SIGMA TEAM:",
    "Greenblood": "- 绿色血",
    "Version": "版权所有 2002-2004 sigma team.                    完全版 1.22",

    # ---------------- [menu] 调试用（俄文注释 ;параметры игрока） ----------------
    "MaxHP": "生命:",
    "MaxSTR": "力量:",
    "MaxSPD": "速度:",
    "MaxACC": "精度:",

    # ---------------- [items] ----------------
    "FindSecret": "发现秘密",

    # 护甲
    "Item204": "装备:绿色护甲",
    "Item205": "装备:黄色护甲",
    "Item206": "装备:红色护甲",
    "Item207": "无敌",

    # 消耗品
    "Item210": "+20 生命",
    "Item211": "+50 生命",
    "Item212": "+100 超级治疗",

    # 装备
    "Item230": "装备:救援包",
    # Item236 第 1 次出现（第 96 行，原文 "EQP:battle dron"）
    "Item236": "装备:战斗无人机",

    # 消耗品
    "Item234": "冻结敌人",
    "Item235": "+1 生命",

    # 武器
    "Item260": "武器:外星枪",
    "Item262": "武器:霰弹枪",
    "Item263": "武器:榴弹发射器",
    "Item264": "武器:转管机枪",
    "Item265": "武器:火箭发射器",
    "Item266": "武器:冷冻步枪",
    "Item267": "武器:等离子步枪",
    "Item268": "武器:火焰喷射器",
    "Item269": "武器:熔岩转管机枪",

    # 消耗品
    "Item301": "装备:炸药",
    "Item302": "弹药:霰弹枪",
    "Item303": "弹药:榴弹发射器",
    "Item304": "弹药:转管机枪",
    "Item305": "弹药:火箭发射器",
    "Item306": "弹药:冷冻步枪",
    "Item307": "弹药:等离子步枪",
    "Item308": "弹药:火焰喷射器",
    "Item309": "弹药:熔岩转管机枪",
    "Item241": "+50 金钱",

    # 植入体
    "Item242": "装备:调整治疗植入体",
    "Item243": "装备:调整力量植入体",
    "Item244": "装备:调整速度植入体",
    "Item245": "装备:调整精度植入体",
}

# 原文件中值带尾随空格的 key -> 尾随空格个数。构建时原样还原，
# 因为那是原作者为菜单对齐刻意留的，不能吞掉。
TRAILING_SPACES = {
    "Low": 1,
    "Item304": 7,
    "Item305": 1,
    "Item308": 2,
    "Item309": 2,
}

# 重复 key 的第 2+ 次出现：直接给出该次出现的译文。
# Item236 在原文件出现两次：
#   第 1 次（第 96 行，原文 "EQP:battle dron"）-> TRANSLATIONS["Item236"] = 装备:战斗无人机
#   第 2 次（第 99 行，原文 "BOMB"）            -> DUP_KEYS["Item236#2"]  = 炸弹
# 注意：原版 INI 解析时后者覆盖前者，所以实际生效的是第 2 行（BOMB）。
# 本表保持与原版一致的行为（不修这个缺陷），只是把两行都译了。
DUP_KEYS = {
    "Item236#2": "炸弹",
}
