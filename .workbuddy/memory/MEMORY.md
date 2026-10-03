# AlienShooterCHS 项目长期记忆

详细逐轮过程见 `2026-10-02.md` / `2026-10-03.md` 日志。本文件只留**可复用结论**。

## 目标与目标程序
让《Alien Shooter》v1.22DIC 显示简体中文。引擎**逐字节**查位图字形 → 汉字节 0xB0-0xF7/0xA1-0xFE
会画成西里尔字母 ⇒ **只要走位图通路必然是俄语，与编码无关**，必须整块接管绘制。

- `I:\LocalGames\AlienShooter v1.22DIC\AlienShooter.exe`，MD5 `641f33c3207d37a838d4f7f8f9c40e26`，
  32-bit PE32，ASLR 开（基址 `0x3E0000`）。无 `CreateTexture`/`LockRect` 直接调用点，无 `SetPixelShader`。
- 注入：**`dsound.dll` 劫持**（该 dll 实为 Ultimate ASI Loader 9.7.4），`winmm.dll` 劫持失败。
  产物 `update\AlienShooterCHS.asi`；唯一日志 `update\chs_probe.log`；清理=删 `update\`。

## 关键 RVA（运行时 = g_exe + RVA）
| RVA | 说明 |
|-----|------|
| `0x026680` | 文本块绘制主循环（hook 目标），prologue `55 8B EC 6A FF` |
| `0x0306A0` | `sub_4306A0`：D3D9 位图字形渲染器（**不是 GDI**，死代码）|
| `0x0D9314` | IAT 槽 `__imp_Direct3DCreate9`，唯一调用点 `0x42DB4D` |
| `0x102AD4` | screen 单例 `dword_502AD4`；`+0xE24`=IDirect3D9*，`+0xE28`=引擎设备，`+0xE34`=字形器(恒 0) |
| `0x17F80`/`0x17F50` | 字形查找 / 存在性判定 |
| `0x122C20` | 全局字体/脚本上下文；`0x24C4F0` = 字形回退对象（RVA）|

Block（`__thiscall`）：`+0x0C`curChar `+0x1C`font `+0x30/0x34`posX/posY `+0x74`**char\*** 文本
`+0x7C`len `+0x80`flags `+0x84/0x88`linesV/linesH。
字体对象：`+0x1C`charWidth `+0x20`lineHeight `+0x70`type(>126 位图 / ≤126 GDI) `+0x384/0x388` 偏移。

## ★ 铁律（血泪，照做）
1. **thiscall 的 ecx 必须恢复**：`push ecx/push ecx/call Probe/add esp,4/pop ecx`+trampoline
   （字节 `51 51 E8 .. 83 C4 04 59`）。每次改动复验。
2. **"少解一层指针"是头号错误源（3 次）**：`void** vt = *(void***)obj;`。改前 `grep "void\*\* vt"`。
3. **无直接调用点的虚表槽位不许硬编码** ⇒ 换导出函数（`D3DXCreateTexture`）或运行时探测（`CjkProbeTex`），全程 `__try`。
4. **`__stdcall` 参数个数猜错 ⇒ ESP 偏移 ⇒ 返回地址被踩 ⇒ 跳栈执行**，`__try/__except` 抓不住。
   不确定的调用走 `CjkSafeCall3/5`（`mov esp,ebp` 自恢复），承接缓冲给足（`pad[256]`）。
5. **指针非空 ≠ 有效**：COM 虚表前先 `LooksLikeDevice` + `IsBadReadPtr(vt,0x1B0)` + `InD3d9()`。
6. 别拿"不确定正确的检查"当准入门槛，准入交给返回值判定的 `CjkProbeTex()`。
7. **渲染状态必须逐项 Get/Set 还回**（不要 `StateBlock9`，连坑两轮）；读不到原状态就 `return -1` 不画。
8. 找 NUL 的循环必须 `while (i<CAP-1 && s[i])`，到界就停。
9. D3D 枚举常量逐个核对真值（已栽 `ALPHATESTENABLE`/`D3DPOOL_MANAGED`/`D3DTA_TEXTURE`）。
10. 留痕日志用**倒计数**；`if (counter<N)` 会无限刷 + 每次 FlushFileBuffers 拖死游戏。
11. 开关文件名前缀匹配会误撞同前缀备份 ⇒ 备份/改名换前缀。
12. **★★★ 不许所有人写同一块纹理再各自 draw**（D3D9 命令缓冲异步 ⇒ 只有最后上传的落屏 ⇒ 闪烁）。
    ⇒ `ManAtlasAlloc()` 给每个使用方**独占**区域 + 1px 透明边。
13. atlas 变大不拖慢上传（`LockRect` 锁子矩形），别为此缩尺寸。
14. **★★★ `DrawPrimitiveUP`：被别的绘制隔开的调用只有最后一笔落屏**（两次紧挨着→都在；中间夹引擎→只剩最后一个）
    ⇒ 自己的 draw 必须**攒成一批、一次画完**，放在引擎画完之后。
15. **★ 引擎不调 `BeginScene/EndScene/Present` 虚表**（计数全 0）⇒ 帧边界**不能靠时间/签名猜**
    （同帧间隔 15~63ms ⇒ 切碎成整帧空白），必须用代码插桩的帧末钩子。
16. 环境量（矩阵/视口/相机）缓存窗口 ≤ 帧长（曾用 1s ⇒ 游戏内文本跟相机漂移；现 2ms）。

## 拿设备 + 帧末落点（唯一可靠链路）
改 IAT `Direct3DCreate9` → IDirect3D9\* → 换虚表 idx16 `CreateDevice` → Device9\* →
idx42 `EndScene` / idx17 `Present`（**实测从不被调用**）⇒ 帧末改**代码**：
RVA `0x3041E` 的 `8B 87 28 0E 00 00`(6B) → `E9 rel32+90`，裸汇编 pushad/pushfd → `ManFrameEndFlush` →
popfd/popad → 复原 → `jmp [g_frameResume]`(=`exe+0x30424`)。装不上自动退旧行为。

### 虚表索引（仅下列 = 引擎自己就在用，可放心）
```
IDirect3D9: CreateDevice=16
Device9: 16 Reset 17 Present 18 GetBackBuffer 41 BeginScene 42 EndScene 45 GetTransform
 48 GetViewport 57 SetRenderState 58 GetRS 59 CreateStateBlock 64 GetTexture 65 SetTexture
 66/67 TSS 68/69 SS 83 DrawPrimitiveUP 89 SetFVF 90 GetFVF 100 SetStreamSource 107 SetVS
Texture9: LockRect=19 / UnlockRect=20（运行时探测；pitch=2560）
ID3DXFont: 13 DrawTextA 14 DrawTextW 15 LostDev 16 ResetDev（已弃用）
```
挖 ground truth：扫 `.text` 里 `call dword ptr [reg+disp32]`，按 disp 聚类反推签名。

### D3D 常量真值（核对过）
```
POOL DEFAULT0 MANAGED1 SYSTEMMEM2 SCRATCH3 | TA DIFFUSE0 CURRENT1 TEXTURE2 TFACTOR3
RS ZENABLE7 ALPHATESTENABLE15 SRCBLEND19 DESTBLEND20 CULLMODE22 ALPHABLENDENABLE27 LIGHTING137
BLEND SRCALPHA5 INVSRCALPHA6 CULL_NONE1 | TSS COLOROP1 COLORARG1/2=2/3 ALPHAOP4 ALPHAARG1/2=5/6
SAMP ADDRESSU1 ADDRESSV2 MAGFILTER5 MINFILTER6 | TEXF NONE0 LINEAR2 CLAMP3
FVF XYZRHW0x4 DIFFUSE0x40 TEX1=0x100 ⇒ MAN_FVF=0x144 | PT_TRIANGLELIST=4
```

## 字体（2026-10-03 实测结论）
配置 `update\chs.ini`：`[font] file/face/quality/scale`、`[layout] xoff/yoff/mode`(0自动 1屏幕 2矩阵)。
`AddFontResourceExW(FR_PRIVATE)`，族名读 ttf `name` 表（0x409 优先→0x804），`GetTextFaceW` 回读校验。
改 ini 重启即生效。

- **"开源字体显小"不是 bug**，是字体自报的垂直度量：`lfHeight`=字符**单元格**高，`cell=(winAsc+winDesc)/upem`：
  SimSun upem256 cell 256(1.000em) ⇒ @18 墨高 16.52px；思源黑体 HW SC upem1000 cell1448(1.448em) ⇒ @18 墨高 11.42px（比 0.69）。
  补偿 `lfHeight *= cell/upem`。
- **"位置偏下"** = 单元格顶 ≠ 墨迹顶：`墨迹顶偏移 = lfHeight*winAsc/(winAsc+winDesc) − em*yMax/upem`
  （思源@18 = 3.98px；补偿后@26 = 5.74px）。修法：按墨迹 bbox 对齐锚点，或 ini `yoff=` 手工补。

## 崩史一句话版
字段类型认错(1) / ecx 未恢复(2) / 状态块未创建(3) / 少解指针×3(4) / 虚表索引写错 4 次(5,7) /
越界写 `WCHAR[256]`(8) / 不还原状态⇒花屏(10) / `__stdcall` 参数不符⇒跳栈(11) /
`D3DPOOL_MANAGED` 写成 3⇒全不可见(15) / 成功路径也置 fail⇒只画第一行(16) /
每项 FlushFileBuffers⇒卡(17) / 同块纹理⇒闪烁(18) / 被隔开只落最后一笔(19) / 猜帧⇒整帧空白(20)。

## 排障手段
- **VEH**（`AddVectoredExceptionHandler`）在装 hook **之前**装好，落盘异常码/EIP/寄存器/`inProbe`。
- `/MAP` + `EIP−基址=RVA` → `.map` 里 grep 函数名 → 函数内偏移 → 字节反汇编锁源码。
- 自证：绘制前后各读 `GetRenderState`/`GetFVF` 比对（`ALL RESTORED OK` / `!!! STILL LEAKING`）；DIB 非零像素数 `lit`。
- **★ 自己起游戏截屏**（`tools/run_game_shot.py`）：起 exe → 点进菜单 → `SetWindowPos(TOPMOST)`+`SetForegroundWindow` → `ImageGrab` → 杀进程。
  ★ 无法区分两种可能时**直接截图**，别加探针让用户回报。
  （`D3DXSaveSurfaceToFileA` 存 backbuffer 不行：`0x8876086C`，须先 `GetRenderTargetData`。）
- IDA MCP：`idb_open{run_auto_analysis:false}` 复用 `.i64`；**字段类型靠 disasm 形态，不靠反编译器**。

## 构建
```
PY="C:/Users/haojun0823/.workbuddy/binaries/python/envs/default/Scripts/python.exe"
cmd //c build.bat                 # MSVC 14.16 / SDK 10.0.26100.0 /MAP；显式设 INCLUDE+LIB，不调 vcvars32
cp dist/AlienShooterCHS.dll "I:/LocalGames/AlienShooter v1.22DIC/update/AlienShooterCHS.asi"
"$PY" tools/build_chs.py [--gb2312]      # 重建文本资源
"$PY" tools/make_gbk_deploy.py           # 部署 GBK 到游戏 Text\ + strings.ini
"$PY" tools/make_import_report.py
```
产物 `dist\AlienShooterCHS.dll`，**不链 CRT**（`/NODEFAULTLIB` + 入口 `CHS_DllEntry` + 自实现 memcpy/memset）。
freestanding 需自供 `__load_config_used`（16 DWORD，首值 0x40）+ `__except_handler3`（MSVC 三个 lib 里没有）。
环境：`reg.exe` 被禁→用 Python `winreg`；PowerShell 无输出→用 Bash+Python。

## 方案 C 速查（自建 GDI→D3D9 渲染器，D3DX 字体已弃用）
引擎坐标是**逻辑坐标**（锚点 −288..−266 / −101..358），当屏幕像素用会推出屏。
栅格化 `CreateFontW`(GB2312_CHARSET, SimSun, ANTIALIASED) 缓存 ≤6 HFONT → 32bpp top-down DIB →
`D3DXCreateTexture`(导出函数，8 参) 1024×256 A8R8G8B8 **MANAGED**；`ManAtlasAlloc()` shelf 打包；
只锁子矩形上传（灰度进 A 通道，顶点色负责颜色）；`ManDrawLine` 只登记，帧末 `ManFrameEndFlush` 一次
`DrawPrimitiveUP`；顶点 28B（`x,y,z,rhw;color;u,v`）；`CjkUpdateMapping()` 2ms 缓存把逻辑→屏幕。
文本编码自己 `MultiByteToWideChar`（默认 CP_ACP/GBK，`chs_utf8.txt` 存在才走 UTF-8；**不能先试 UTF-8**：
GBK `0xB0A1` 是合法 UTF-8）。posX/posY 是锚点，bit0/1=左/中/右，bit3/2=顶/中/底；宽度**必须自己量**。
兜底 `g_manFail`(可重试)/`g_manFatal`(永久)，连踩 8 次自动关闭接管；逃生开关 `update\chs_off*`。

## 文本资源
`Text/*.txt` = ASCII 4007B + 真中文 8624B + 俄文注释 174B；`strings.ini` value 全中文。
译文表 `tools/translations_zh.py`(Text 966 行) / `tools/translations_ini_zh.py`(ini)。

## 状态与待办（2026-10-03）
**中文在所有界面正常显示**（主菜单/设置/选任务，见 `tools_out/verify_*.png`）。
1. 选任务界面「生命/力量/敏捷」三标签重叠（字距/行距）2. 颜色全白，ESC 颜色码丢弃，需找引擎颜色表
3. 中英混排：ASCII 也被接管变宋体风格 → 考虑纯 ASCII 放行 4. 性能：状态保存每项一次→可帧内复用
5. 字位偏下补偿（yoff 或 bbox 对齐）
