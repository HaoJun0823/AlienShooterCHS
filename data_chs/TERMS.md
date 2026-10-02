# Alien Shooter 简体中文本地化 —— 术语表

术语表是全表一致性的唯一依据。改动任何一条，先改这里，再改 `translations_zh.py`。

## 核心名词

| 英文 | 中文 | 备注 |
|------|------|------|
| alien / aliens | 外星人 | 不译「异形」，与「怪物」严格区分 |
| monster / monsters | 怪物 | 变异生物，不译「丧尸」 |
| alien clone | 外星人克隆体 | level_07 |
| teleportator / teleporter | 传送器 | 原文字母拼写不一，统一 |
| virus | 病毒 | |
| virus culture / strain | 病毒样本 | |
| mutants | 变异体 | |

## 场景 / 建筑

| 英文 | 中文 |
|------|------|
| base | 基地 |
| housing estate | 住宅区 |
| research center | 研究中心 |
| laboratory / labs | 实验室 |
| training complex | 训练基地 |
| testing laboratory | 测试实验室 |
| office block | 办公区 |
| conference building | 会议大楼 |
| school | 学校 |
| prison | 监狱 |
| warehouse | 仓库 |

## 武器 / 弹药

| 英文 | 中文 |
|------|------|
| two pistols | 双持手枪 |
| shotgun | 霰弹枪 |
| grenade launcher | 榴弹发射器 |
| minigun | 转管机枪 |
| rocket launcher | 火箭发射器 |
| freeze rifle | 冷冻步枪 |
| plasma rifle | 等离子步枪 |
| flame thrower | 火焰喷射器 |
| magma minigun | 熔岩转管机枪 |
| shotgun shells | 霰弹枪弹壳 |
| minigun shell box | 转管机枪弹箱 |
| plasma ammo box | 等离子弹药箱 |
| magma ammo box | 熔岩弹药箱 |
| freeze ammo | 冷冻弹 |

## 道具 / 装备

| 英文 | 中文 |
|------|------|
| implant | 植入体 |
| armor | 护甲 |
| RED / BLUE / YELLOW / GREEN | 红 / 蓝 / 黄 / 绿 |
| capacity | 防护 |
| hit damage | 受损 |
| life | 生命 |
| flashlight | 手电筒 |
| night vision | 夜视仪 |
| rescue bag | 救援包 |
| drone | 无人机 |

## UI 结构标签

| 英文 | 中文 |
|------|------|
| MISSION NN | 任务 NN |
| DESCRIPTION | 任务简报（关卡）/ 描述（道具、武器） |
| TIPS | 提示 |
| OBJECTIVE | 目标 |
| PARAMETERS | 参数 |
| damage | 伤害 |
| radius | 范围 |
| reload | 装填 |
| speed | 速度 |

## 参数枚举值

| 英文 | 中文 |
|------|------|
| INSTANTLY | 即时 |
| NORMAL | 普通 |
| FAST | 快速 |
| FASTEST | 最快 |
| SLOW | 缓慢 |
| FREEZE | 冻结 |

## 排版约定

- 冒号一律用半角 `:`（与原文件一致，贴合窄文本框）
- 破折号用 `——`，仅在 GB2312 版本降级为 `--`（GB2312 无此字符）
- 姓名间隔号用 `·`，GB2312 版本降级为 `・`
- 感叹号用半角 `!`，与原文件风格一致
- 省略号用 `……`
