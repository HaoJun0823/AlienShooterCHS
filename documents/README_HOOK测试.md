# CHS Hook 探测版 — 使用说明

## 这是什么

~~一个只观测、不改行为的 DLL~~ —— **探测阶段已完成**。
方向 A（复用引擎自带通路）**已实机证伪**，当前是**方向 B：自建 D3DX 字体接管 CJK**。

### 阶段一：探测（已完成）

纯观测，采集游戏文本渲染的真实数据。结论：

| 项 | 结果 |
|------|------|
| 崩溃 | 0 次（45,000 次调用跑满）|
| `badBlock` | 0 —— Block 偏移与类型全部实测正确 |
| `gdiPath` | 0 —— 100% 走位图字体 |
| 字节分布 | 纯 ASCII 0x20–0x79，无高位字节（当时还没上中文文本）|
| `charWidth` | `[6..13]`，三种字体并存 |

### 阶段二：方向 A —— 复用引擎自带通路（**已实机证伪，废弃**）

~~见下方同名章节~~ 保留原文是为了记录为什么走不通，别再试第二次。

### 阶段三：方向 B —— 自建 D3DX 字体接管 CJK（**当前**）

见下方「方向 B」章节。

## 产物

```
dist\AlienShooterCHS.dll    (12.8 KB, 纯 Win32 API, 无 CRT 依赖)
deploy_gbk\Text\*.txt       71 个 GB18030 中文文本（部署到游戏 Text\）
deploy_gbk\strings.ini      GB18030 中文界面字符串
backup\original_text\       游戏原始英文文本的完整备份
```

## 方向 A：把 CJK 交给引擎自带的 GDI 通路（❌ 已废弃）

> **2026-10-03 实机结论：这条路走不通，原因有两个，都是硬伤。**
> 下面这段分析保留原样，仅作存档 —— 别再照着它实现。

### 核心发现：引擎本来就有一条 TrueType 通路，只是从来没走过

`sub_426680` 的入口就有分流：

```asm
4266AA  mov ebx, [esi+1Ch]              ; ebx = Block->font
4266C1  cmp dword ptr [ebx+70h], 7Eh    ; font->type
4266C5  jle loc_426A05                  ; <= 126 → 整块交给 GDI
```

`loc_426A05` 就是跳板：

```asm
426A0E  push dword ptr [esi+74h]        ; Block->text
426A11  movss xmm0, [esi+34h]           ; posY
426A16  mov ecx, dword_502AD4           ; 屏幕/渲染器单例
426A2F  call sub_4306A0
```

`sub_4306A0` 的签名（`retn 10h` 证明 4 个栈参数）：

```c
sub_4306A0(this = dword_502AD4, float x, float y, const char* text, int n = -1)
```

内部转发 `sub_41B710(this = [dword_502AD4 + 0xE34], x, y, -1, text, 0)`，
最终落到 `CreateFontA` + `GetTextExtentPoint32A` + `ExtTextOutA`。

`CreateFontA` 唯一调用点 `0x41C3EF` 的参数：

| 参数 | 值 | 意义 |
|------|----|----|
| `iCharSet` | **1 = `DEFAULT_CHARSET`** | 允许系统按区域挑字体 + 字体链接 ← 关键 |
| `iQuality` | 4 = `ANTIALIASED_QUALITY` | 抗锯齿 |
| `SetTextColor` | `0xFFFFFF` | 白色 |
| `lfFaceName` | 来自配置结构体 `[ebp-168h]` | 可在配置里指定 |

### 为什么中文能显示

Win32 的 `*A` 版 API 用 **CP_ACP** 解释字节串，中文 Windows 下即 **CP936/GBK**。
所以只要磁盘上是 GBK 字节，`ExtTextOutA` 就能拿到正确的汉字 ——

这正是 `tools/make_gbk_deploy.py` 存在的理由：**UTF-8 的中文是 3 字节序列，
直接喂进 ANSI 管线会变成一堆不可名状的字符。**

### 接管策略

```
每次 sub_426680 被调用（ecx = Block）：
  ├─ 文本里没有 >=0x80 的字节  → 无条件放行，走原位图字体（零改动、零风险）
  └─ 有                        → ① UTF-8→GBK 转换
                                  ② 复现对齐/偏移计算（见下）
                                  ③ 调 sub_4306A0 用 GDI 画整块
                                  ④ 直接 ret，**跳过原位图流程**
```

任何一步没把握（设施缺失 / 指针可疑 / 转换失败）就返回 0，回退到原路径。

#### 对齐必须自己复现

`sub_426680` 在正式画之前会依据 `alignFlags` 修正 `posX/posY`
（`426775`–`426853`）。少了这段，文字整体会错位：

```c
if (flags & 1)       px = px - (linesH-1) * charWidth * k;      // k = 0.5f
else if (flags & 2)  px = px - ((linesH-1) * charWidth + font->offsetX);
else                 px = font->offsetX + px;

if (flags & 8)       py = py - (linesV-1) * lineHeight * k;
else if (flags & 4)  py = py - ((linesV-1) * lineHeight + font->offsetY);
else                 py = font->offsetY + py;
```

`k` 来自 `dword_4D9434`，实测 = `0.5f`。

### 最大的未知

`sub_4306A0` 开头：

```asm
4306A6  cmp dword ptr [esi+0E34h], 0
4306AD  jz  loc_4306F9        ; ← 为 NULL 就直接返回，什么都不画
```

这条 GDI 通路在游戏里**从未被使用**（探测日志 `fontType` 恒 >126），
所以 `[screen + 0xE34]` 上的 GDI surface 可能从未被创建。

DLL 安装时会做一次设施自检并写进日志：

```
[facility] screen(dword_502AD4) = 004xxxxx
[facility] screen+0xE34 (GDI surface) = 00000000  -> NULL (sub_4306A0 would no-op!)
```

**如果日志里是 `NULL`，就说明这条路是死的**，需要换个悬挂点（见下节备选方案）。

---

## ❌ 方向 A 的实机结果：崩了，而且证伪

2026-10-03 03:29 跑了一次游戏，`update\chs_probe.log` 里的现场：

```
[facility] screen(dword_502AD4) = 0
[facility] screen+0xE34 (GDI surface) = 0  -> NULL
[probe #1] block=9393888

=== CRASH ===
code     = 0xC0000005
EIP      = 3FB734  (inside AlienShooter.exe, +0x1B734)
inProbe  = 0
EAX=07E2831C ECX=00000000 EDX=004106A0 EBX=17085420
ESI=65547465 EDI=09393888 EBP=012FF638 ESP=012FF604
calls    = 1
=== END CRASH ===
```

`EDX=004106A0` 是 `sub_4306A0` 的地址（调用残留），`EIP` 落在
`sub_41B710 + 0x24`。看那段汇编：

```asm
41B72B  mov eax, [esi+1074h]
41B731  push eax
41B732  mov ecx, [eax]
41B734  call dword ptr [ecx+10h]    ; ★ 崩在这：ECX=00000000
```

`EAX = 07E2831C` 是个合法堆地址，但 `[EAX] = 0` —— **虚表是空的**。

### 两条硬伤

**① `sub_4306A0` 根本不是 GDI，是 D3D9 位图字形渲染器。**

```asm
4306D3  mov ecx, [esi+0E34h]      ; this = 字形渲染器对象（不是 surface）
4306F4  call sub_41B710
```

`sub_41B710` 逐字符从 `[this+0x74 + 16*ch]` 取字形度量
（left / top / right / bottom 四个 float，一条 16 字节），手填顶点缓冲后画三角形：

```asm
41B74E  call [ecx+164h]   ; SetFVF(0x144) = XYZRHW|DIFFUSE|TEX1，步长 0x1C=28
41B7C7  call [ecx+2Ch]    ; IDirect3DVertexBuffer9::Lock(0,0,&p,DISCARD)
41BB0B  call [ecx+144h]   ; DrawPrimitive(D3DPT_TRIANGLELIST, 0, n)
```

字形度量表长度 = `(0x1074 - 0x74) / 16` = **256 条**。
**这本身就是位图字体，装不下汉字。**

**② 这条通路是没初始化过的死代码。**

`[this+0x1074]` / `[this+0x1078]` 是两个 `IDirect3DStateBlock9*`
（虚表 `0x10`=Capture、`0x14`=Apply），但游戏从来没 `CreateStateBlock` 过它们 ——
所以第一次进去就在 `Capture()` 上炸了。

顺带纠错：`sub_4306A0` 的真实签名是

```c
sub_4306A0(screen, float x, float y, int color, const char* text)
```

不是 `(x, y, text, n)`。方向 A 把 `-1` 当成 text 传了进去（幸好先崩在状态块）。

---

## 方向 B：自建 D3DX 字体接管 CJK（当前）

思路：不再借引擎的字形表，**自己拿 D3D 设备建一个 TrueType 字体来画**。

### v2（2026-10-03 第二轮实机后重写）

第一轮实机 `noDev=10499`、`drawn=0` —— 设备**一次都没拿到**，所以中文一个字
也没接管，画面上全是俄文字形。原因见下节。v2 把取设备的方式整个换掉。

| 环节 | 做法 |
|------|------|
| ❌ 取设备（v1，失败） | `[[screen+0xE34] + 0x5C]` —— 那个字形渲染器是**懒创建**的，本作从不走那条通路，它压根没被 new，实机恒为 0 |
| ✅ 取设备（v2） | **改 IAT + 换虚表**：`Direct3DCreate9` → `IDirect3D9::CreateDevice` → 拿到设备 |
| 设备校验 | 虚表前 4 项必须落在 `d3d9.dll` 映像范围内 —— 防止把野指针喂给 D3DX |
| 兜底 | 扫 `screen+0xE00..0xF00`，找「`vt[0]/vt[41]/vt[42]/vt[118]` 全在 d3d9.dll 内」的指针（IDirect3D9 只有 17 项会被这条判据排除）|
| 建字体 | `d3dx9_43.dll!D3DXCreateFontA`（游戏已加载该 DLL，导出实测存在）<br>`CharSet = GB2312_CHARSET`，face = `"SimSun"`，`ANTIALIASED_QUALITY`，按 (device, height) 缓存 4 个 |
| 文本 | 自己 `MultiByteToWideChar`，**默认 `CP_ACP`（= GBK）**；`update\chs_utf8.txt` 存在时改 UTF-8。<br>★ 不能"先试 UTF-8 再回退 GBK"：GBK 的 `0xB0 0xA1` 恰好是**合法 UTF-8 序列**（会解成西里尔 U+0421）|
| 绘制 | `ID3DXFont::DrawTextW`（**虚表索引 14**），`pSprite = NULL` |
| 时机 | 队列（96 条上限）+ **`EndScene`（索引 42）或 `Present`（索引 17）之前统一刷**。<br>★ 本作实测**两个都不调**（`endScene=0 present=0` 跑了 8000 次绘制），所以实际是"入队即刷" |
| 设备重置 | 换 `Reset`（虚表 16）→ 前后各调 `OnLostDevice` / `OnResetDevice` |
| 字号 | **直接用 `font+0x20`（lineHeight）**，钳到 `[8,72]`。<br>★ 不要乘任何缩放：`sub_426680` 逐字推进 `[ebx+1Ch]`、换行推进 `[ebx+20h]`，<br>全程没有系数 —— 它们**就是屏幕像素**。<br>（`sub_41B710` 那条 `[TR+0x6C]/[TR+0x70]` 缩放属于**死代码**通路，<br>照它算会得出 `sy=8.0` 把字号顶到 72，实测错的）|
| 坐标 | **锚点语义**（见下）：`posX/posY` 本身就是对齐锚点，不是左上角 |
| 控制符 | `ESC`+颜色码整段丢弃、`\r` 丢弃、`\t`→空格、`\n` 断行（最多 32 行）|
| 失败兜底 | 任何一步不成就 `return 0` 回退原位图通路 —— 最坏只是不显示中文，不崩 |
| **SEH 兜底** | `D3DXCreateFontA` / `DrawTextW` 外包 `__try/__except`：记异常码 + 出错地址 + 故障地址，**连踩 8 次自动关闭接管**。最坏是"没中文"，绝不会是"游戏崩了" |
| 虚表项校验 | 调 `DrawTextW` 前先确认指针落在 `d3dx9_43.dll` 映像内，否则记 `badVtbl` 并拒绝调用 |

#### freestanding 下启用 MSVC SEH（本项目特有）

`/NODEFAULTLIB` 不链 CRT，但 `__try/__except` 需要两个 CRT 符号：

| 符号 | 解决办法 |
|------|---------|
| `__load_config_used` | 自己定义在 `.rdata$lcfg` 段：16 个 DWORD，首值 `0x40`（`SEHandlerTable`/`Count` = 0，等价 `/SAFESEH:NO`）。C 里写 `_load_config_used`，x86 装饰后正好是 `__load_config_used` |
| `__except_handler3` | **VC 的 `msvcrt.lib` / `libcmt.lib` / `libvcruntime.lib` 里都没有**。但 `msvcrt.dll` 导出了 `_except_handler3`，且 MSVC x86 生成的正是 handler3 作用域表 → 写裸跳转桩等价于导入 thunk：<br>`__declspec(naked) _except_handler3(...) { __asm { jmp dword ptr [g_pExceptHandler3] } }`<br>字节 `FF 25 xx xx xx xx`。★ 必须在任何 `__try` 之前 `GetProcAddress` 填好，否则跳 0 地址 |

#### 捕获链路（IDA 旁证）

```
42DB4D  call Direct3DCreate9        ← 全 exe 唯一调用点
42DB52  mov ecx, eax
42DB54  mov [edi+0E24h], ecx        ← edi = screen(dword_502AD4)
                                      ⇒ screen+0xE24 = IDirect3D9*
```
据此改写 exe 导入表槽 `__imp_Direct3DCreate9`（RVA `0x0D9314`），
时序上没问题 —— ASI loader 在进程初始化阶段就加载本 DLL，早于游戏 `WinMain`。

虚表索引速查：`IDirect3D9::CreateDevice = 16`；
`IDirect3DDevice9`：Reset=16、Present=17、BeginScene=41、EndScene=42、GetViewport=47。

**★ `ID3DXFont` 虚表（索引，不是字节偏移）—— 第五轮实机就错在这**

```
 0 QueryInterface    1 AddRef          2 Release
 3 GetDevice         4 GetDescA        5 GetDescW
 6 GetTextMetricsA   7 GetTextMetricsW
 8 GetDC             9 GetGlyphData
10 PreloadGlyphs    11 PreloadTextA   12 PreloadTextW
13 DrawTextA        14 DrawTextW
15 OnLostDevice     16 OnResetDevice
```

旧代码按「GetDescW 之后紧接着就是 DrawText」写成 8/9/10/11，漏了中间
五项，于是 `vt[9]` 实际是 `GetGlyphData`：

```
GetGlyphData(font, Glyph=pSprite(NULL), ppTexture=pString,
             pBlackBox=Count(-1), pCellInc=pRect)
→ d3dx9_43 内部解引用 pBlackBox = 0xFFFFFFFF，当场 AV
```

实测现场完全吻合：`EIP` 在 d3dx9_43.dll 内、`EDI=FFFFFFFF`、
`fault=FFFFFFFF READ`、`EAX=0`。

**判据千万别只靠"指针在模块内"** —— `GetGlyphData` 也是 d3dx9_43 的
真函数，范围校验照样通过。现在建完字体会先 `GetDevice(3)` + `GetDescW(5)`
反查一次（设备指针是否原样返回、字号字体名是否回读一致），
再用 `DT_CALCRECT` 量一次宽度，索引错了当场能看出来。

#### 对齐必须自己重算（锚点语义）

`sub_426680` 的对齐算式配合字形绘制，等价于：

| 标志位 | 含义 | `posX` 实际是 |
|--------|------|---------------|
| 0（默认） | 左对齐 | **左边缘** |
| bit0 (`1`) | 水平居中 | **整行中心** |
| bit1 (`2`) | 右对齐 | **右边缘** |

（字形以 `x` 为中心绘制，所以默认分支才要 `+ ox`，而 `ox = cw/2`；
中间/底部同理，`oy = lh/2`。）

但**宽度必须自己量**：引擎按**字节数**算宽度（逐字节推进 `charWidth`），
汉字 2 字节 1 个字形，而我们的字形宽只有 `fontH` 而不是 `2*cw`。
照抄 `linesH` 会把整段推出屏幕（第四轮实测 `x = -286`）。
所以现在只存锚点 + 对齐位，宽度到 `CjkFlush` 用 `DT_CALCRECT` 实测。

### ★★ 为什么先前的中文显示成"俄语乱码"

引擎按**单字节**查位图图集，而 `002font.tga`（128×160 / 8×16 格）里
0x80+ 位置装的是**西里尔字母**。GBK 汉字的字节正好落在 `0xB0-0xF7` /
`0xA1-0xFE` —— 于是每个字节都被画成了一个俄文字母。

**只要还走位图通路，中文必然显示成俄语，与文本编码无关。**
所以光换文本编码没用，必须整块接管绘制（这正是方向 B 在做的事）。

### 逃生开关

在 `update\` 目录下放一个 **`chs_off.txt`**（内容随意），DLL 会完全关闭
CJK 接管，走回原位图通路。这样万一新方案在机器上出问题，
**不用重新编译 DLL 也能立刻恢复可玩**。

### 本次要看的日志

```
[d3d] IAT Direct3DCreate9 hooked: 6F89xxxx -> 6FCDxxxx (real export=6F89xxxx)
[d3d] Direct3DCreate9(32) -> 07xxxxxx ; CreateDevice orig=6F89xxxx
[d3d] CreateDevice -> dev=07xxxxxx looksOK=1
[d3d]   Reset orig=6F89xxxx  EndScene orig=6F89xxxx  Present orig=6F89xxxx
[cjk] source encoding = CP_ACP/GBK (default)
[cjk] D3DXCreateFontA(dev=07xxxxxx, h=18, cs=134) -> hr=0x00000000 font=07xxxxxx
[cjk #1] at=(120,240) h=18 nl=1 drawHr=0x00000000 endScene=1
```

| 字段 | 判读 |
|------|------|
| `Direct3DCreate9(32) ->` 非 0 | IAT 钩子在正确时机生效了 |
| `looksOK=1` | 设备指针可信（虚表在 d3d9.dll 内）|
| `EndScene orig=` / `Present orig=` 非 0 | 队列刷写机制挂上了 |
| `hr=0x00000000` 且 `font` 非 0 | 字体建成了 |
| `drawHr=0x00000000` | 真画上去了 |
| `drawHr=0x88760868` | `D3DERR_INVALIDCALL` —— 仍不在有效渲染状态，<br>**不崩但没画面**，需再换落点 |
| `endScene=N / present=N`（统计行） | 哪个落点真的在被调。两个都 0 说明都没挂上 |
| `[d3d] fallback scan` | IAT 钩子没赶上时启用的兜底扫描，会把找到的偏移写进日志 |
| `[cjk] KILL SWITCH` | 探测到 `chs_off.txt`，接管被关闭 |

#### ★★ 第四轮（v4）为何字体建成后仍然崩溃

`CjkDrawLineW()` 里第三次犯了同一个错误：

```c
void** vt = (void**)font;          // ← 错：vt[9] 读到的是对象数据成员
long hr = dt(...);                 // ← 把垃圾值当函数指针 call 出去
```

日志里的证据完全吻合：

```
[cjk] D3DXCreateFontA(dev=88844C0, h=72, cs=134) -> hr=0x00000000 font=A1B5E8
=== CRASH ===
EIP = 768FCA5E  (垃圾地址，"outside known modules")
EAX=0085000F EDX=0085000F EBX=0085000F        ← 全是 ID3DXFont 对象里的字段
[cjk #1] ... drawHr=0xFFFFFC19                 ← -999 = 初始值，DrawTextW 从未返回
```

`CjkFontsOnLost` / `CjkFontsOnReset`（`vt[10]`/`vt[11]`）同错，一并修。
**`grep "void\*\* vt"` 一共 6 处，其中 3 处错 —— 改一处必须查全部。**

#### ★ 第二轮（v2）为何仍然 `drawn=0`

`LooksLikeDevice()` 里把**对象当成了虚表**：

```c
void** vt = (void**)dev;      // ← 错：vt[0] 是虚表指针，vt[1..3] 是对象数据成员
void** vt = *(void***)dev;    // ← 对（VtblReplace / CjkScanDevice 都是这么写的）
```

于是 `vt[1..3]` 读到堆地址，必然不在 `d3d9.dll` 范围内 → 校验恒假 →
`noDev` 一路涨、`drawn=0`，而设备其实早就抓到了。

## 注入方式：dsound.dll 劫持（已实测）

游戏目录里放的是 **Ultimate ASI Loader** 伪装成 **`dsound.dll`**（不是 `winmm.dll`！）：

| 项 | 值 |
|----|----|
| 文件 | `I:\LocalGames\AlienShooter v1.22DIC\dsound.dll` |
| 真实身份 | Ultimate ASI Loader 9.7.4（ThirteenAG，MIT）|
| 大小 | 5418336 字节，导出 1334 个符号 |
| MD5 | `c4aa7183a88decf99fa695eb8fcdda6c` |

### 为什么是 dsound 不是 winmm

两个 DLL **都在 exe 导入表里**（`WINMM.dll` 6 个函数、`DSOUND.dll` 1 个序号导入），
也都**不在 Windows 的 KnownDLLs 列表**里（已用注册表核实，38 项里没有 winmm/dsound），
理论上都能被 sideload 劫持。

但**实测 `winmm.dll` 劫持失败，`dsound.dll` 劫持成功**。原因未最终定位 ——
游戏可能对 winmm 走了不同的初始化路径。目前以实测为准，用 dsound。

> ⚠️ 之前分析报告里写「winmm 劫持是注入链起点」是**错的**，已更正。

### DLL 放哪都行

loader 扫描**游戏根目录**及 `scripts\ / plugins\ / update\` 下的 `*.asi`。
实测五个副本全部被加载（根目录 `.asi`、根目录 `.dll`、`scripts\`、`plugins\`、`update\`）。

本项目统一放 **`update\AlienShooterCHS.asi`** —— 与日志同目录，清理时删一个目录即可。

## 怎么用

### 1. 部署

```bash
cp dist/AlienShooterCHS.dll "I:/LocalGames/AlienShooter v1.22DIC/update/AlienShooterCHS.asi"
```

### 2. 正常启动游戏

玩一会儿，把**菜单、HUD、物品提示、开场文本**都看一遍。

### 3. 看日志

**只有一个文件**：

```
I:\LocalGames\AlienShooter v1.22DIC\update\chs_probe.log
```

统计会**自动落盘**：每累积 2000 次绘制调用写一段 `[auto-dump]`，
无需外部调用任何导出函数。

### 4. （可选）打印文本内容

默认只统计字节分布。想看具体字符串，让 loader 在加载后调用：

```c
typedef void (__cdecl *SetDumpFn)(int);
GetProcAddress(h, "CHS_SetTextDump")(1);
```

打开后每次调用都会记录文本内容（最多 200 条），形如：

```
[142] len=18 align=0x0 linesH=1 linesV=1 w=13.00 h=18.00 x=100.00 y=200.00 text="PRESS SPACEBAR TO CONTINUE"
```

## 日志长什么样

```
=== AlienShooterCHS probe ===
pid       = 62956
loaded as = I:\LocalGames\AlienShooter v1.22DIC\update\AlienShooterCHS.asi
self path = I:\LocalGames\AlienShooter v1.22DIC\update\AlienShooterCHS.asi
exe base  = 003E0000
target    = 00406680 (RVA 0x026680)
log path  = I:\LocalGames\AlienShooter v1.22DIC\update\chs_probe.log

[install] base  = 3E0000
[install] target= 406680 (RVA 0x026680)
[install] E9 written rel=0xFFF1A2C8
[install] hook addr = 7280B97B
[install] write-back verified OK
[install] hook armed

[auto-dump @ call=2000]
printable=8123 ctrl=0 esc=44 nl=210 tab=3 fontTag=0 gdiPath=17
charWidth=[13..13]
align: [0]=800 [1]=0 ...
fontType(+0x70): [0]=0 [1]=0 ...
byte histogram (0x20+ only):
  0x20  --  512
  0x41  A   733
  ...
```

注意 **`exe base = 003E0000` 不是 `0x400000`** —— ASLR 生效了，
每次运行都不一样。所有地址都靠 `g_exe + RVA` 算，这是能工作的前提。

## 我从日志里想看什么

| 字段 | 用途 |
|------|------|
| 字节直方图各字节计数 | **确认实际用到的码位集合** —— 决定字形图集要覆盖哪些码 |
| `charWidth` 区间 | 确认字宽是否恒定，决定排版算法 |
| `align` 分布 | 确认对齐标志实际怎么用（静态反汇编只是推测） |
| `fontType(+0x70)` 分布 | 有多少文本走位图字体、多少走 GDI |
| `len` / `linesH` / `linesV` | 验证 `Block` 结构体字段语义是否正确 |
| `fontTag` 出现次数 | 确认 `<Font=` 字体切换标签的实际使用频率 |
| `gdiPath` 占比 | 如果很多文本走 GDI，那部分不用管 CJK |

**最关键的是字节直方图。** 它会直接告诉我们：这个游戏实际显示的字符
到底落在哪些字节上。如果只有 ASCII 和少量俄文，那字形图集方案就定了；
如果发现意料外的高位字节，说明还有我没逆到的加载路径。

## 崩溃史（踩过的坑，按发现顺序）

### ① prologue 校验字节写错 → hook 静默不装
校验 `55 8B EC 68`，实际是 `55 8B EC 6A FF`（`push -1` 编成 imm8）。
永远不匹配，日志只有一句 ABORT，连实际字节都看不到。

### ② trampoline 回跳 +8 → 应为 +5
那 3 条指令只占 5 字节，+8 切在 `push 4D4768h`（SEH handler）中间，
SEH 装不上 → 崩溃。

### ③ 日志格式化不认 `%02X` / `%lX`
解析完 flags/width/precision 直接进 switch，漏了长度修饰符 `l`。
结果 `E9 written rel=0x%lX` 整串退化成字面量 —— 排障时被严重误导。

### ④ `Block+0x74` 类型写错：char** 应为 char*
```
426740  mov eax, [esi+74h]
426748  mov dl, [eax]        ← 直接当 char* 用
```
偏移对的，类型错了 → 多解一层指针 → 野地址。
**教训：字段的"类型"和"偏移"同等重要，只核对偏移看不出错。**

### ⑤ ★★ thiscall 的 ecx 没恢复 → 第一次绘制就崩（本次）
```
4266A8  mov esi, ecx         ← ecx 就是 this
4266AA  mov ebx, [esi+1Ch]
```
hook 里 `push ecx; call ProbeBlock` 之后直接跳回原函数。
`ProbeBlock` 是 `__cdecl`，**ecx 属于 caller-saved**，编译器随便糟蹋它 →
`mov esi, ecx` 拿到垃圾 → `[esi+1Ch]` 访问违例。

表现很有迷惑性：日志显示 `hook armed` 一切正常，然后立刻崩，
**一条 ProbeBlock 记录都没有**（因为崩在它返回之后）。

修复：压两份 ecx，一份当参数、一份留着恢复。
```
51            push ecx     ; 保存 this
51            push ecx     ; 参数
E8 A9000000   call ProbeBlock
83 C4 04      add esp, 4
59            pop ecx      ; 恢复 this
```

## 崩溃取证设施

探测 DLL 跑在游戏主循环里，它一崩就是整个进程崩，而日志会停在
`hook armed`，看不出崩在哪。所以装了两层取证：

| 机制 | 作用 |
|------|------|
| **VEH**（`AddVectoredExceptionHandler`，XP 回退 `SetUnhandledExceptionFilter`）| 任何异常先落盘再交给原处理链：异常码、EIP、全部通用寄存器、`calls` 计数 |
| **`g_inProbe`** | 崩时非 0 = 异常发生在探测代码内部；0 = trampoline / 原函数出的事 |
| **前 8 次调用留痕** | `[probe #N] block=...`，一次都没有说明崩在进入探针之前 |
| **EIP 归属判定** | 打印 `INSIDE AlienShooterCHS` / `inside AlienShooter.exe` + 偏移，直接定位是谁的代码 |

崩溃时日志末尾会出现：
```
=== CRASH ===
code     = 0xC0000005
EIP      = 004066A8  (inside AlienShooter.exe, +0x266A8)
inProbe  = 0
EAX=... ECX=... EDX=... EBX=...
ESI=... EDI=... EBP=... ESP=...
calls    = 1
=== END CRASH ===
```

自动 dump 阈值从 2000 降到 **500**，且每次写完 `FlushFileBuffers` ——
崩过一次才知道"能跑多久"没有保证，别让数据卡在缓存里。

## 安全设计

- **不修改任何游戏数据**，只读 Block 结构体字段
- **逐字段可读性校验**：`ProbeBlock` 入口先 `IsBadReadPtr(block, 0x90)`，
  每个字段读取前再单独校验 —— 探测代码本身绝不能成为崩溃源
- **prologue 校验**：写跳板前先核对前 5 字节是否为 `55 8B EC 6A FF`，
  不匹配就打印实际字节并放弃安装（防止版本变化时乱跳崩溃）
- **写后读回校验**：E9 写完立即读回比对，5 字节全对才算装上
- **ASLR 安全**：所有地址用 `g_exe + RVA` 运行时计算
- **无 CRT**：不注入额外运行时，也不依赖 CRT 初始化顺序
- **跳板可回退**：trampoline 复现原函数前 5 字节（`55 8B EC 6A FF`）后
  接回 `target+5`，由原函数自己的 epilogue 返回，栈平衡由原代码保证

## 崩溃史（留档避免重犯）

**第 1 次实机崩溃（2026-10-02 23:44）**

日志显示 hook 装上了（`write-back verified OK` / `hook armed`），
随后崩溃在首次绘制时。原因：

```c
// 错误写法
static inline char** BlockText(void* b) { return *(char***)((char*)b + 0x74); }
char* text = BlockText(block) ? *BlockText(block) : NULL;   // 多解一层指针
```

IDA 反汇编核对（`sub_426680`）：

```
426740  mov eax, [esi+74h]      ← +0x74 直接就是 char*
426748  mov dl, [eax]           ← 当字符串用
```

`+0x74` 是 **`char*`** 不是 `char**`。多解一层 → 读到野地址 → 崩。

修正：`BlockText` 返回 `char*`，调用侧不再解引用。
同时给 `ProbeBlock` 加上逐字段 `IsBadReadPtr` 守卫，
并新增 `badBlock` 计数（被拦下的不可读访问次数）便于诊断。

**教训**：结构体偏移的"类型"和"偏移"同等重要。
只核对偏移（`+0x74` 确实对）而没核对类型（`char*` vs `char**`），
静态分析看起来完全正确，实机照样崩。**每个字段都要对照反汇编确认类型。**

### ★★★ 虚表索引：同一个坑埋了四次（第 5/7/10/11 轮）

**根因**：`ID3D*` 接口的槽位**唯一权威来源是 `d3d9.h`**，
而这个环境里没有 SDK 头文件。"按接口继承层次数出来"是不可靠的直觉 ——
COM 头部继承链长度因接口而异，凭印象数必然出错。

| 轮次 | 猜的值 | 实际 | 后果 |
|------|--------|------|------|
| 5 | `ID3DXFont::DrawTextW`=9 | **14** | 调到 `GetGlyphData`，崩 |
| 7 | `Device::CreateTexture`=22 | **错**（返回 `0xDEADBEEF`）| 中文画不出去 |
| 9 | `Texture::LockRect`=8/9 | **19/20** | 崩 |
| 11 | 同上，改为推导 14/15 | **19/20** | 仍然是错的 |

**最终确立的两条硬规则**：

**① 能从引擎二进制实测的，必须实测。**
引擎自己渲染正常 ⇒ 它用的索引必然正确。扫 `.text` 里所有
`call dword ptr [reg+disp32]`（`FF 90..97` + disp32），按 disp 聚类得候选槽位，
再按调用前的 `push` 立即数反推签名：
```
57 SetRenderState ← 75 处调用，push 0Eh/13h/1Bh + 值
65 SetTexture      ← push 纹理指针 + 0
67 SetTextureStageState  69 SetSamplerState
83 DrawPrimitiveUP ← push 2, 6
89 SetFVF          ← push 144h（XYZRHW|DIFFUSE|TEX1）
100 SetStreamSource ← push 1Ch（= 顶点结构步长 28，反证顶点布局正确）
107 SetVertexShader ← push 0（解绑）
```

**② 实测不到的，一律运行时探测 —— 且调用必须走安全桩。**
- **优先换导出函数**：`D3DXCreateTexture` 替代 `Device::CreateTexture`，零索引风险
- **其次是探测**：`CjkProbeTex` 扫纹理虚表找 LockRect，
  判据 `hr==0 && pBits!=NULL && Pitch%4==0 && Pitch>=64`
- **`CreateStateBlock`（=59）**：由实测锚点 57 顺推，与 65/67/69 三点互验

**③ ★★★ 探测调用必须走 `CjkSafeCall3`/`CjkSafeCall5`（第 12 轮血泪）**

第 12 轮崩溃现场：
```
EIP = 20F69C (outside known modules)   fault = 20F69C WRITE
ECX = ESI = EIP = 20F69C               ESP = 20F664   ← EIP 就在栈上
```
**`EIP` 落在栈地址 = 跳到栈上执行**。原因：D3D9 全部方法是 `__stdcall`，
callee 用 `ret N` **自己清栈**。盲扫时按错的参数个数调用（比如把 `Present`
当成 3 参数的 `CreateStateBlock`），callee 多弹 8 字节 ⇒ ESP 偏移 ⇒
返回地址被踩。

> ★ **`__try/__except` 抓不住这类错误** —— 它不是异常，是栈被一点点啃掉。
> 这就是为什么那一轮"零日志、直接崩在栈上"。

对策：
```c
extern "C" __declspec(naked) long __cdecl CjkSafeCall3(void* fn, void* a1, void* a2, void* a3)
{
    __asm {
        push ebx / push esi / push edi / push ebp
        mov eax,[esp+20]  mov ebx,[esp+24]  mov ecx,[esp+28]  mov edx,[esp+32]
        push edx / push ecx / push ebx
        mov ebp, esp              // 记住压完参数的 ESP
        test eax,eax / jz skip
        call eax
    skip:
        mov esp, ebp              // ★ 无条件恢复 ESP，无视 callee 的 ret N
        add esp, 12
        pop ebp / pop edi / pop esi / pop ebx / ret
    }
}
```
**另外：承接输出的缓冲必须给足。** 原 `CjkProbeTex` 用 8 字节 `ManLockedRect`
接结果，若那个槽其实是 `GetLevelDesc`，它会写 ~124 字节的 `D3DDESC9` → 砸栈。
已改用 `pad[256]`。

### ★★ 改了渲染状态不还原 → UI 彻底损坏、黑白屏（第 11 轮）

日志 `manHr=0`（**绘制是成功的**），但 UI 全毁。原因：hook 插在
`sub_426680`（引擎文本绘制循环）中间，此时引擎的渲染批次正进行到一半。
我们改了：
```
8 个 SetRenderState（ZENABLE / ALPHABLENDENABLE / SRCBLEND / DESTBLEND /
                   ALPHATEST / CULLMODE / LIGHTING / FOGENABLE）
+ 6 个 SetTextureStageState + 3 个 SetSamplerState
+ SetFVF(0x144) + SetVertexShader(NULL) + SetTexture(0, g_tex)
```
**一个都没还原**，引擎后续绘制全部继承 ⇒ 画面错乱。
只 `SetTexture(0, NULL)` 恢复一项是杯水车薪。

**解法**：`IDirect3DStateBlock9` 的 `Capture`（槽位 3）/ `Apply`（槽位 4）
整包保存恢复 —— 天然覆盖 FVF / VS / 纹理绑定 / 各级状态，不可能漏项。
**拿不到状态块就完全不画**（`ManEnsure` 直接 `return 0`），绝不"改完不还"。

**★ 正确生命周期（第 13 轮用一次 d3d9 内部崩溃换来的）**：

```c
sb = CreateStateBlock(D3DSBT_ALL, &sb);   // ← 创建时就立即捕获引擎当前状态
... 改状态 + 绘制 ...
sb->Apply();       // 整包还原
sb->Release();     // 释放
```

> **绝对不要在 `CreateStateBlock` 之后再调 `Capture()`。**
> D3D9 规范里 `Capture` **只用于 `BeginStateBlock` 录制的状态块**。
> 多调一次的直接后果（第 13 轮实机）：
> ```
> EIP = 6F90A200 (d3d9.dll, RVA 0x7A200)   fault = 0 WRITE   ESI = 0
> ```
> 崩在 d3d9 内部函数中段，日志里连一行 `[man]` 都没有。
> `CreateStateBlock` 槽位 = **59**，由 57 顺推、与 65/67/69 三点互验后实机确认。

顺带：**不需要显式 `SetVertexShader(NULL)`** —— D3D9 里 `SetFVF` 会自动把
顶点着色器置 NULL；而 live 代码里根本没有 `SetVertexShader` 调用点。少一次
未知槽位调用就少一个风险源。

### ★ 其它必知的坑

| 坑 | 说明 |
|----|------|
| **「设备指针非空」≠「设备指针有效」** | 设备 Reset/丢失后 `dev` 是野指针，`*(void***)dev` 得 NULL，读 `dvt[57]`(=+0xE4) 就崩（第 10 轮）。碰任何虚表前先 `LooksLikeDevice` + `IsBadReadPtr(vt, 0x1B0)`（0x1B0=108*4 覆盖最大槽 107*4）|
| **函数指针要过范围校验** | 索引错一位会读到"恰好非空"的相邻槽，`call` 过去就是随机崩。每个取到的函数指针都要 `InD3d9()` |
| **`D3DXCreateTexture` 吐的是原厂对象** | 虚表在 **d3d9.dll** 而非 d3dx9_43.dll。`InD3dx` 只用于 D3DX **导出函数指针**；所有 **COM 虚表项**用 `InD3d9`（第 8 轮）|
| **「找 NUL 再推进」的循环要有硬上限** | `pos += k+1` 在整段无 NUL 时会越过 `WCHAR[256]` 末尾，下一轮就是野指针（第 9 轮）。写成 `while (i < CAP-1 && s[i]) ++i;` 且到界就停 |
| **不要拿"不确定是否正确的检查"当准入门槛** | `GetLevelDesc` 槽位也没实测过，第 7 轮它索引不对 → 误杀一个好纹理 → `fatal=1`。已降级为纯诊断 |
| **逃生开关的通配符会误触发** | `chs_off*` 是**前缀匹配**（`FindFirstFileA`），任何**以 `chs_off` 开头**的文件都会关掉接管 —— 包括 `.bak` 备份（第 16 轮就这样白白浪费一轮）。备份/改名时必须换掉前缀。日志会明确回报 `KILL SWITCH: found "xxx" -> DISABLED` 或 `no chs_off* -> takeover ACTIVE` |
| **`strings.ini` 里的 `0xA7xx` 不是 bug** | 那是**俄文注释**（`;название игры...`），按设计「注释不译」。真正的 value 全是中文（`Lvl=关卡` / `Exit=退出`）。核验编码时别被它误导 |
| **D3D 枚举常量的值必须逐个核对** | 和虚表索引同类风险。已经栽过三次：`D3DRS_ALPHATESTENABLE` 写成 24（真值 15，24 是 `ALPHAREF`）、`D3DPOOL_MANAGED` 写成 3（真值 1，3 是 `SCRATCH`）、`D3DTA_TEXTURE` 写成 1（真值 2，1 是 `CURRENT`）|
| **留痕日志必须用倒计数** | 写成 `if (counter < N)` 的话，只要计数不增就**永远为真** ⇒ 每次绘制都 `LogPrintf + FlushFileBuffers`（第 17 轮实测 206 次刷盘，游戏直接卡死）。改成 `if (left > 0) { --left; }` 结构上就不可能失控 |

### ★★★ 两种「所有自检都绿、画面却空白」的根因（第 16/17 轮）

这两轮的日志特征极有诊断价值，先记判据：

```
[man] traceA: LockRect...   ← 206 次
[man] traceB: ... lit=206   ← 206 次（DIB 里确实有字形像素）
[man] traceC: BEFORE ...    ← 只有 1 次   ★ 差异就在这里
[man] trace5: ... ALL RESTORED OK  ← 只有 1 次
```

`traceA/B` 跑满而 `traceC/trace5` 只有 1 次 ⇒ **第 2 次起在两者之间提前 return**。
原因是自己写的一个标志位：

```c
if (g_gettersFail == -1) return -1;
g_gettersFail = -1;        // ← 成功路径也置成"失败" ⇒ 第 2 次起永久停画
```

**⇒ 只有第一块文字被画出来，其余全空白。**
教训：**"只报一次日志"的正确写法是 `if (!reported) { reported = 1; log(); }`，
绝不能用业务状态位兼职。**

**② 更根本的一层：`D3DPOOL_MANAGED_` 写成了 3**

```c
#define D3DPOOL_MANAGED_ 3   // ★ 3 是 D3DPOOL_SCRATCH！MANAGED 真值 = 1
```

`D3DPOOL_SCRATCH` 的纹理**永远不能被设备使用**（只能当暂存介质）。于是：

| 环节 | 表现 |
|------|------|
| `LockRect` / `UnlockRect` | 完全正常，`pitch=2560=640×4` 精确吻合 ⇒ 探测判据全部通过 |
| `SetTexture(0, tex)` | 返回成功 |
| **采样** | **读到的是垃圾 ⇒ 中文整块不可见** |

这就是为什么"虚表探测、尺寸回读、状态还原、像素栅格化（`lit>0`）全部自检通过，
画面却一个字都没有"。

**⇒ 通用教训：纹理"能建、能锁、能绑"不等于"能被采样"。池（Pool）选错时
前三个环节一点异常都不会报。**

### ★★★ 第三种「自检全绿但画面不全」：所有人写同一块纹理再各自 draw

中文上屏后出现：**一个界面里只有其中一项中文显示，而且这一项会闪。**

诊断证据（日志里的绘制顺序 + 用户的描述）：

| 绘制顺序 | #4 | #5 | #6 | #7 | #8 | **#9（最后画的）** |
|---|---|---|---|---|---|---|
| 文本 | 继续 | 退出 | 制作人员 | 最高分 | 选项 | **新游戏** |

用户说显示的是「**新游戏**」⇒ **就是最后被绘制的那一项**。

机理：当时所有文本项都往纹理的 **同一个 (0,0) 区域**上传，而 D3D9 是
命令缓冲 + 异步执行（本作还跑在 Win10 的 D3D9 转译层上，批处理更激进），
GPU 真正执行 draw 时纹理里已经是**最后一次上传**的内容：

| 项 | 结果 |
|---|---|
| 最后画的那项 | UV 与内容匹配 ⇒ **正确显示** |
| 其它项 | 按各自 w/h 取样到同一块内容 ⇒ 多数取到黑底/碎片 ⇒ 看起来"不显示" |
| 每帧顺序一变 | 可见的项就换人 ⇒ **闪烁** |

**修法**：`ManAtlasAlloc()` 给每个文本项分配**独占**的 atlas 区域
（shelf 打包：行内向右，放不下换行；单调前进、绕回可重用 —— 绕回时
早先那些 draw 早已执行完，所以重用是安全的）。

```c
int aw = w + MAN_SLOT_PAD * 2, ah = h + MAN_SLOT_PAD * 2;
if (!ManAtlasAlloc(aw, ah, &sx, &sy)) {
    ManAtlasReset();                                  // 绕回一圈后重试
    if (!ManAtlasAlloc(aw, ah, &sx, &sy)) return -1;
}
```

三个配套细节，漏一个都会出问题：

| 细节 | 原因 |
|------|------|
| 槽四周留 `MAN_SLOT_PAD=1` 的透明边 | 线性过滤在边界会取到邻格，没黑边就会被隔壁槽的内容污染（文字边缘混进别的字的碎片）|
| UV / `LockRect` 子矩形 / DIB 读起点 / 循环范围**同步改成槽位** | 只改一半就会画到错误区域 |
| 槽左上角对应屏幕 `(left-(PAD+1), ay-(PAD+1))` | 文字在槽内偏移 `PAD+1`，这样落点与改造前**逐像素一致** |

**atlas 现在是 1024×256**（曾一度改成 640×96，是误判）：`LockRect` 锁的是
**子矩形**，所以 atlas 变大不会拖慢单次上传 —— 当时的问题不是 atlas 太大，
而是所有项都往 (0,0) 挤。按 shelf 打包 1024×256 约能放 90+ 项，一帧足够。

### ★★★ 第四种：多次 `DrawPrimitiveUP` 之间隔着别的绘制 → 只活最后一笔

上面三个坑都修完之后，现象变成：**一个界面里只有一项中文显示，而且会闪。**

决定性证据链（全部实机）：

| # | 做法 | 结果 |
|---|------|------|
| 1 | 每个文字上方画一个**纯色小方块**（不依赖纹理） | 只看到**一个**方块 |
| 2 | 放 `chs_off.txt` 关掉接管，让引擎自己画 | **6 项全部显示**（俄文）⇒ 引擎的 draw 全部落屏 |
| 3 | 引擎二进制确认 idx 83 = `DrawPrimitiveUP`（6 处调用） | 我们的槽位没写错 |
| 4 | **一次** `DrawPrimitiveUP` 画 2 个四边形 | 两个都出现 ✅ |
| 5 | **两次**调用、**紧挨着**、中间无状态改动 | 两个都出现 ✅ |
| 6 | 两次调用、**中间隔着引擎的绘制 / 状态改动** | **只剩最后一个** ❌ |

**⇒ 本作这个 D3D9 实现下，"被别的绘制隔开"的 `DrawPrimitiveUP` 只有最后一笔能落屏。**

（同时确认：`clear=1`、`begin=0`、`present(dev/swapchain)=0/0` ——
既不是清屏，引擎也**不调 BeginScene/EndScene/Present**，拿不到任何帧回调。）

### 解法：攒成一批，每项重画整批

```c
// 每识别出一项：
ManEmitQuad(...);        // 只登记四边形（不画）
ManDrawQuads(dev);       // 把"本帧到目前为止的全部四边形"一次画出去
```

为什么"重画全部"？——前面那些被引擎绘制隔开的批次本来就作废，
**最后一次必然包含本帧全部内容**，所以最终画面是完整的。
`ManDrawQuads` 里状态**只保存一次 / 设置一次 / 还原一次**，所有四边形走同一条
`DrawPrimitiveUP`（`TRIANGLELIST`、`STRIDE=28`、`nQuads*2` 个图元）。

**帧边界**：既然拿不到 `BeginScene/EndScene/Present`，就用**业务特征**判：
> 同一个 `(文本, 位置, 字号)` 在本批里**重复出现** = 新的一帧开始了 ⇒ 整批作废

实测效果（`tools_out/verify_*.png`）：主菜单 6 项、设置界面、选任务界面
的中文**全部正常**，日志 `batch #1..N quads=1..9 hr=0` 每帧 1→9 后重置、`trapped=0`。

### ★ 排障方法：自己起游戏截屏，别只靠用户描述

到这一步"用户描述"已经无法区分「只有一项落屏」和「全部落在同一位置」了。
正确做法是**在进程外自己取证**：`tools/run_game_shot.py`
（起 exe → 点击进菜单 → 窗口置顶 → `ImageGrab` 截游戏窗口 → 杀进程）。
第 21 轮靠它 + `chs_off.txt` 对照组，几分钟定位。
注意 `D3DXSaveSurfaceToFileA` 存 backbuffer **不行**（返回 `0xD3DERR_INVALIDCALL`），
要先 `GetRenderTargetData` 弄到 SYSTEMMEM 表面。

### ★ 崩溃定位手段：链接器 `/MAP`

`build.bat` 的 link 加 `/MAP:"%OUT%\AlienShooterCHS.map"`。崩溃时：
```
EIP - 模块基址 = RVA  →  在 .map 里 grep 函数名得 RVA  →  相减得函数内偏移
→ 配合该 RVA 的字节反汇编即可锁定源码位置
```
比"读代码猜哪里错"快得多，也不依赖 PDB 符号解析。
（`.map` 格式：`0001:00003ac0  ?ManDrawLine@@...  10003ac0 f  hook.obj`，
 段号 `0001` 是 `.text`，偏移 `0x3ac0` 即 RVA。）

## 已知限制

1. 目标函数带 SEH frame + `/GS` stack cookie。本 hook 只是原样搬运
   prologue，**不涉及 SEH 状态转移** —— 这是安全的。但如果后续要改成
   「hook 中段再跳回」，必须重新处理 SEH。

2. `Block` 结构体偏移**已用 IDA 反汇编逐条核对**（含字段类型）。
   但 `len` / `linesH` / `linesV` 的**语义**（每个字段具体代表什么）
   仍未验证 —— 日志里如果出现离谱的值，立刻告诉我。

3. `g_exe` 必须在 `CHS_Hook` 之前赋值。当前在 `DllMain` 里
   `GetModuleHandleA(NULL)` 拿基址，ASI loader 的加载时机足够早，安全。

## 下一步（取决于日志结果）

拿到字节直方图后：

- **如果码位集中在 ASCII + 少量高位** → 做 CJK 字形图集，
  替换 `sub_417F80` 的字形查找 + 扩展容量
- **如果发现大量 GDI 路径** → 分两条线处理，位图和 GDI 各一套
- **如果 `Block` 字段语义与推测不符** → 先修正结构体定义再往下走

## 源码

| 文件 | 说明 |
|------|------|
| `AlienShooterCHS/hook.cpp` | hook 安装、trampoline、探测逻辑 |
| `AlienShooterCHS/block_layout.h` | Block 结构体偏移 + 引擎符号 RVA |
| `AlienShooterCHS/dllmain.cpp` | DLL 入口 |
| `build.bat` | 构建脚本（Release/Win32, MSVC v141, SDK 10.0.26100.0） |

自行重新编译：

```
build.bat
```

## 构建环境备注

`build.bat` 显式设置 `INCLUDE`/`LIB` 而不调用 `vcvars32.bat`，
因为构建跑在受限沙箱里，vcvars 会调用被禁用的 `reg.exe`。
在正常环境下用 VS 的「x86 Native Tools 命令提示符」也能直接编译。
