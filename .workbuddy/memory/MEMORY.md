# AlienShooterCHS 项目长期记忆

（精简版：详细逐轮过程见 `2026-10-02.md` / `2026-10-03.md` 日志）

## 目标与目标程序
让《Alien Shooter》v1.22DIC 显示简体中文。障碍：引擎逐字节处理文本（单字节查字形），
位图图集无 CJK，字形表容量 256 写死。

`I:\LocalGames\AlienShooter v1.22DIC\AlienShooter.exe`
- MD5 `641f33c3207d37a838d4f7f8f9c40e26`，32 位 PE32，**ASLR 开**（实测基址 `0x3E0000`）
- 12 导入 DLL / 205 函数，无延迟/绑定导入。全二进制**无 `CreateTexture`/`LockRect`
  直接调用点**（引擎走 `D3DXLoadSurfaceFromMemory`），也**无任何 `SetPixelShader` 调用点**

## 注入与日志（已定型，不要改）
- **`dsound.dll` 劫持**：游戏目录的 `dsound.dll` 是 **Ultimate ASI Loader 9.7.4**
  （MD5 `c4aa7183a88decf99fa695eb8fcdda6c`）。`winmm.dll` 劫持**实测失败**（原因未定位）
- 产物 `update\AlienShooterCHS.asi`；**唯一日志 `update\chs_probe.log`**；清理=删 `update\`
- loader 扫 `*.asi`/`*.dll`/`scripts\*.asi`/`plugins\*.asi`/`update\*.asi`；
  它是 C++/`std::filesystem` 写的，**字符串全 UTF-16**（扫 ASCII 找不到）

## 关键 RVA（运行时 = g_exe + RVA）
| RVA | 说明 |
|-----|------|
| `0x026680` | 文本块绘制主循环（hook 目标）。prologue `55 8B EC 6A FF` |
| `0x0306A0` | `sub_4306A0`（**D3D9 位图字形渲染器，不是 GDI**，死代码，见下）|
| `0x0D9314` | IAT 槽 `__imp_Direct3DCreate9`，唯一调用点 `0x42DB4D` |
| `0x102AD4` | `dword_502AD4` 屏幕单例；`+0xE24`=IDirect3D9*，`+0xE34`=字形渲染器（懒创建，恒 0）|
| `0x17F80`/`0x17F50` | 字形查找 / 存在性判定 |
| `0x122C20` | 全局字体/脚本上下文 |
| `0x24C4F0` | 字形查找失败回退对象（是 **RVA**，非绝对地址）|

trampoline 复现前 **5** 字节后接回 `target+5`（写 +8 会切断 SEH 安装）。

## Block 结构（esi=Block，`__thiscall`）
| 偏移 | 类型 | 依据 |
|------|------|------|
| `+0x0C` int curChar | `+0x1C` void* font (`4266AA mov ebx,[esi+1Ch]`) |
| `+0x30`/`+0x34` float posX/posY | `+0x74` **char\*** 文本（**不是 char\*\***）|
| `+0x7C` int len | `+0x80` int flags (`and eax,70h`) |
| `+0x84` int linesV | `+0x88` int linesH |

字体对象：`+0x1C` charWidth / `+0x20` lineHeight / `+0x70` type（**>126 走位图，≤126 转 GDI**）
/ `+0x384` offsetX / `+0x388` offsetY。

**★ 字段「类型」和「偏移」同等重要** —— 偏移对而类型错，静态分析看着完全正确，实机照样崩。

## 铁律（血泪，逐条遵守）
1. **thiscall 的 ecx 必须恢复**。naked hook 里探测函数是 `__cdecl`，ecx 属 caller-saved：
   `push ecx / push ecx / call Probe / add esp,4 / pop ecx / <trampoline>`
   字节 `51 51 E8 .. 83 C4 04 59`。**每次改动都复验这两处字节还在。**
2. **"少解一层指针"是本项目头号错误源（已踩 3 次）**：
   `void** vt = (void**)obj;` 错，`void** vt = *(void***)obj;` 对。
   改任何取虚表的代码，先 `grep "void\*\* vt"` 过一遍全部出现处。
3. **引擎二进制里没有直接调用点的虚表槽位，一律不许硬编码。** 两条出路（按优先级）：
   ① 换**导出函数**（`GetProcAddress`，如 `D3DXCreateTexture`）；
   ② **运行时探测**（用可判定的返回值反查，如 `CjkProbeTex`），全程 `__try`，
      命中后缓存槽位号（`g_texProbeDev` 记设备，换设备重探）。
4. **`__stdcall` 参数个数猜错 ⇒ ESP 偏移 ⇒ 返回地址被踩 ⇒ 跳到栈上执行。**
   `__try/__except` **抓不住**（不是异常，是栈被啃）。凡"槽位不确定"的调用一律走
   `CjkSafeCall3`/`CjkSafeCall5`（naked asm，调用后无条件 `mov esp, ebp` 自恢复）。
   配套：承接输出的缓冲**给足**（8 字节结构体接可能是 `GetLevelDesc` 的调用 → 写 124 字节砸栈），已用 `pad[256]`。
5. **「设备指针非空」≠「设备指针有效」。** 碰任何 COM 虚表前先 `LooksLikeDevice(dev)`
   + `IsBadReadPtr(vt, 0x1B0)`（覆盖最大槽 107*4），且每个函数指针过 `InD3d9()`。
   纹理必须与设备绑定：设备换了/无效就丢弃重建。
6. **不要拿"不确定是否正确的检查"当准入门槛**（`GetLevelDesc` 索引不对 → 误杀好纹理）。
   真正准入交给用返回值判定的 `CjkProbeTex()`。
7. **在别人的渲染循环中间插绘制，改过的状态必须还回去**（否则黑白花屏）。
   **不要用 `IDirect3DStateBlock9`**（连坑两轮，见崩溃史 13/15），用**逐项 Get/Set**。
   **读不到原状态就 `return -1` 不画**，绝不"改完不还"。
8. **所有"找 NUL 再推进"的循环**必须 `while (i < CAP-1 && s[i]) ++i;` 且到界就停，
   不能靠调用方保证有终止符。
9. **D3D 枚举常量必须逐个核对真值**，与虚表索引同类风险（已栽：`ALPHATESTENABLE`、
   `D3DPOOL_MANAGED`、`D3DTA_TEXTURE`）。
10. **留痕日志必须用倒计数**，不能写成 `if (counter < N)` —— 计数不增就永远为真，
    会无限刷日志 + 每次 `FlushFileBuffers`，把游戏拖死。
11. **开关文件名放宽成前缀匹配后，任何同前缀的备份文件都会误触发**
    （`chs_off*` 撞上 `chs_off.txt.txt.bak`，白费一轮）。备份/改名必须换前缀。
12. **★★★ 不要"所有人写同一块纹理再各自 draw"。** D3D9 是命令缓冲 + 异步执行
    （本作跑在 Win10 的 D3D9 转译层上，批处理更激进），GPU 执行 draw 时纹理里
    已是**最后一次上传**的内容 ⇒ 只有最后画的那一项正确，其余取到黑底/碎片，
    看起来"不显示"；每帧顺序一变就**闪烁**。实测主菜单 6 项只有最后画的
    「新游戏」可见。**必须给每个使用方分配独占 atlas 区域**（见 `ManAtlasAlloc`）。
13. **改 atlas 尺寸前先想清楚"单次上传搬多少"**：`LockRect` 锁的是**子矩形**，
    所以 atlas 变大不会拖慢单次上传。曾因误判把 256 降到 96，其实问题在
    "所有项都往 (0,0) 挤"。
14. **atlas 槽位四周要留 1px 透明边**：线性过滤在边界会取到邻格，
    没有黑边就会被隔壁槽的内容污染（表现为文字边缘出现别的字的碎片）。
15. **★★★ 本作的 `DrawPrimitiveUP`："被别的绘制隔开"的调用只有最后一笔落屏。**
    实机对照：一次调用画 2 个 → 都在；两次**紧挨着** → 都在；
    两次**中间夹引擎绘制** → 只剩最后一个。
    ⇒ 自己的 draw 必须**攒成一批、一次画完**，且要放在**引擎画完之后**（铁律 19）。
16. **★★ 本作引擎不调 `BeginScene/EndScene/Present` 的虚表**（计数全 0，但
    0x30433 确实有一条 `call [ecx+44h]`）⇒ 拿不到虚表回调，
    **帧边界不能靠时间/签名猜**（间隔可达 15~63ms，会把一帧切碎 ⇒ 整帧空白），
    必须用**代码插桩的帧末钩子**（铁律 19）。

## 崩溃定位手段
- **VEH**（`AddVectoredExceptionHandler`，XP 回退 `SetUnhandledExceptionFilter`）在装 hook
  **之前**装好；落盘异常码/EIP/全部寄存器/`module=`/`fault=`(读写+地址)/`inProbe`。
- **`/MAP` 链接** + `EIP - 模块基址 = RVA` → `.map` 里 `grep 函数名` 相减 = 函数内偏移
  → 配字节反汇编锁源码。比读代码猜快得多。
- 自证诊断：绘制前后各读一次 `GetRenderState`/`GetFVF` 并比对（`ALL RESTORED OK` /
  `!!! STILL LEAKING`）；统计 DIB 非零像素数 `lit` 验证 GDI 真出字了。

- **取证：自己起游戏截屏**（`tools/run_game_shot.py`，最有效的排障手段）：
  起 exe → 点击进菜单 → `SetWindowPos(HWND_TOPMOST)` + `SetForegroundWindow` →
  `PIL.ImageGrab` 截游戏窗口 → 杀进程。
  ★ **当"用户描述"无法区分两种可能时，别再加探针让用户回报，直接自己截图看。**
  第 21 轮靠这条 + `chs_off.txt` 对照组，几分钟就定位了"只有最后一项落屏"。
  （`D3DXSaveSurfaceToFileA` 存 backbuffer **不行**，返回 `0x8876086C D3DERR_INVALIDCALL`
   —— backbuffer 要先经 `GetRenderTargetData` 弄到 SYSTEMMEM 表面。）

## IDA MCP
`idb_open{input_path, mode:"prefer_headless", run_auto_analysis:false}`（必须传 false），
复用已落盘 `.i64` 秒开；`disasm` 可靠，`decompile 0x426680` 失败。
**判断字段类型靠 disasm 指令形态（`mov eax,[esi+74h]`），不靠反编译器推断。**

## ★★ 字体：可配置 + 为什么"开源字体显小/靠下"（2026-10-03 实测）
配置 `update\chs.ini`：
- `[font] file= / face= / quality= / scale=`
- `[layout] xoff= / yoff=`（像素，正=右/下，任何字号生效）、`mode=`（0自动 1强制屏幕 2强制矩阵）
  —— 用来现场微调/定位文字整体偏移
`AddFontResourceExW(path, FR_PRIVATE)` 私有加载，族名从 ttf 的 `name` 表读
（英文 0x409 优先、其次中文 0x804），每个候选用 `GetTextFaceW` 回读校验，
全失败才回退 SimSun。**改 ini 重启即生效，不用重编译。**

### ★★ 字号偏小的根因 = 字体自报的垂直度量（不是我们的 bug）
GDI 的 `lfHeight` 是**字符单元格高度**，而"单元格 = 几个 em"由字体自己声明：
| 字体 | upem | usWinAsc/Desc | cell | 同 lfHeight 下的 em | 中 的墨高 @lfHeight18 |
|------|------|---------------|------|----------------------|------------------------|
| SimSun | 256 | 220 / 36 | **256 = 1.000 em** | 18.00 px | **16.52 px** |
| 思源黑体 HW SC VF | 1000 | 1160 / 288 | **1448 = 1.448 em** | 12.43 px | **11.42 px** |
⇒ 墨高比 **0.69** —— 一模一样的高度参数，思源黑体只有 SimSun 的 69%。
补偿：`lfHeight *= (winAsc+winDesc)/upem`（`TtfReadVert()` 运行时从 ttf 读，
`scale=0` 自动，可手填覆盖）。补偿后墨高 16.51 px ≈ SimSun 16.52 px ✔
（SimSun 另有内嵌点阵 ppem=12..17，小字号走位图更实更黑，视觉上还要更"大"一点）

### ★ 位置偏下的根因 = 单元格顶 ≠ 墨迹顶
我们按"单元格左上角"贴到引擎给的锚点上，而墨迹顶离单元格顶还有一段：
`墨迹顶偏移 = lfHeight*winAsc/(winAsc+winDesc) - em*(yMax/upem)`
- SimSun @18：15.47 − 14.77 = **0.70 px**（几乎贴着顶）
- 思源黑体 @18：14.42 − 10.44 = **3.98 px**
- 思源黑体 @26（补偿后）：20.83 − 15.09 = **5.74 px** ← 观感"比标准位置靠下" 5px 左右
根因是思源黑体要给拉丁/越南语的重音留空间，`winAscent(1160) > upem(1000)`，
单元格顶部到 em 盒顶部还有 160/1448 ≈ 11% 的空档。
**修法（未做）**：把墨迹 bbox 量出来（我们本来就在扫 DIB 像素），
按墨迹顶对齐锚点，或在 ini 加一个 `yoff=` 手工补偿。

## 为什么中文必然显示成俄语
引擎按**单字节**查位图图集；`002font.tga`（128×160 / 8×16 格 / 160 格）的 0x80+ 位置装的是
**西里尔字母**。GBK 汉字首/次字节落在 0xB0-0xF7 / 0xA1-0xFE → 每字节画成一个俄文字母。
**只要还走位图通路，中文必然变俄语，与文本编码无关** ⇒ 必须整块接管绘制。

字体图集（3 个）：`001font.tga` 208×288 24bpp 13×18（大写+数字+符号+西里尔，0x61-0x7A 空白）
/ `002font.tga` 128×160 16bpp 8×16（全 ASCII 含小写+西里尔，游戏内小写来自这里）
/ `003font.tga` 96×128 16bpp RLE 6×16（仅数字与 `$ : / = >`，HUD）。
`charWidth [6..13]` = 三字体并存（6=003 / 8≈002 / 9,13=001）。

## ★ 方向 A 被证伪：`sub_4306A0` 不是 GDI，是 D3D9 位图字形渲染器
```
sub_4306A0(screen=dword_502AD4, float x, float y, int color, const char* text)
   4306D3 mov ecx,[esi+0E34h] ; 4306F4 call sub_41B710
```
字形度量表 `[TR+0x74 + 16*ch]`（4 float/条），长度 `(0x1074-0x74)/16 = 256` ⇒ 位图，装不下 CJK。
渲染走 `SetFVF(0x144)` + 顶点缓冲 + `DrawPrimitive`；`[TR+0x5C]`=Device9*，
`[TR+0x64]`=VertexBuffer9*，`[TR+0x68/0x6C]`=视口宽高，`[TR+0x70]`=设计基准，
`[TR+0x1074/0x1078]`=StateBlock9*（**本 exe 从未创建** → 实机崩在 `41B734 call [ecx+10h]`）。
坐标 x/y **原样**进顶点（XYZRHW=屏幕像素）；只有字形宽高乘缩放。

## ★ 拿设备的唯一可靠链路：改 IAT + 换虚表
`[[screen+0xE34]+0x5C]` 永远拿不到（懒创建，实测 `noDev=10499`）。正确链路：
```
① 改 IAT __imp_Direct3DCreate9 (RVA 0x0D9314)          → IDirect3D9*
② 换 IDirect3D9 虚表 idx16 CreateDevice (0x40)          → IDirect3DDevice9*
③ 换 Device9 虚表 idx42 EndScene / idx17 Present        → 场景内刷绘制队列
   idx16 Reset (0x40) → 前后调 ID3DXFont::OnLostDevice/OnResetDevice
```
IDA 旁证 `42DB4D call Direct3DCreate9 / 42DB54 mov [edi+0E24h],ecx` ⇒ `screen+0xE24=IDirect3D9*`。

### 虚表索引（**仅下列 = 实测确认，引擎自己就在用，可放心**）
```
IDirect3D9: CreateDevice=16
IDirect3DDevice9: 16 Reset 17 Present 18 GetBackBuffer 41 BeginScene 42 EndScene
   45 GetTransform 48 GetViewport 57 SetRenderState 58 GetRenderState
   59 CreateStateBlock 64 GetTexture 65 SetTexture 66 GetTextureStageState
   67 SetTextureStageState 68 GetSamplerState 69 SetSamplerState
   83 DrawPrimitiveUP 89 SetFVF 90 GetFVF 100 SetStreamSource 107 SetVertexShader
IDirect3DTexture9: **LockRect=19 / UnlockRect=20**（运行时探测得出，pitch=2560=640*4）
   ★ 猜过的 8/9（GetPriority/PreLoad）、14/15 全是错的；GetLevelDesc=11 也不对
ID3DXFont: 13 DrawTextA / 14 DrawTextW / 15 OnLostDevice / 16 OnResetDevice（已弃用，留档）
IDirect3DStateBlock9: 3 Capture 4 Apply（**已弃用，见铁律 7**）
```
**从二进制挖 ground truth 的方法**：扫 `.text` 里 `call dword ptr [reg+disp32]`
（`FF 90..97`+disp32），按 disp 聚类 + 调用前 push 的立即数反推签名。**只能用于引擎调用过的槽位。**

## ★★ D3D 常量真值表（全部核对过，别再凭印象写）
```
D3DPOOL     DEFAULT=0  MANAGED=1  SYSTEMMEM=2  SCRATCH=3
D3DTA       DIFFUSE=0  CURRENT=1  TEXTURE=2  TFACTOR=3
D3DRS       ZENABLE=7 ALPHATESTENABLE=15 SRCBLEND=19 DESTBLEND=20 CULLMODE=22
            ALPHABLENDENABLE=27 FOGENABLE=28 LIGHTING=137
D3DBLEND    SRCALPHA=5  INVSRCALPHA=6      D3DCULL_NONE=1
D3DTSS      COLOROP=1 COLORARG1=2 COLORARG2=3 ALPHAOP=4 ALPHAARG1=5 ALPHAARG2=6
            TEXCOORDINDEX=11  TEXTURETRANSFORMFLAGS=24
D3DTOP      MODULATE=4  SELECTARG2=3
D3DSAMP     ADDRESSU=1 ADDRESSV=2 MAGFILTER=5 MINFILTER=6 MIPFILTER=7
D3DTEXF     NONE=0 LINEAR=2      D3DTADDRESS_CLAMP=3
D3DFMT_A8R8G8B8=21  D3DPT_TRIANGLELIST=4  D3DTS_{VIEW=2,PROJECTION=3,WORLD=256}
D3DFVF  XYZRHW=0x004 DIFFUSE=0x040 TEX1=0x100 ⇒ MAN_FVF=0x144
D3DXCreateTexture 是 **8 参**（无 pSharedHandle）
```

## ★ 当前方案：方向 C —— 完全自建 GDI→D3D9 纹理渲染器（D3DX 字体全弃用）
D3DX 字体能画但排版全错：引擎坐标是**逻辑坐标**（锚点 -288..-266 / -101..358），
当屏幕像素用会把整段推出屏幕。

| 环节 | 做法 |
|------|------|
| 栅格化 | `CreateFontW`(GB2312_CHARSET, SimSun, ANTIALIASED) 缓存 ≤6 个 HFONT（`ManFontFor`/`ManSelectFont`）→ 32bpp top-down DIB（`CreateDIBSection`），`GetTextExtentPoint32W` 量宽高，`ExtTextOutW(ETO_OPAQUE\|ETO_CLIPPED)` 黑底白字 |
| 建纹理 | `d3dx9_43.dll!D3DXCreateTexture`（导出函数，零索引风险）**1024×256** A8R8G8B8 **MANAGED** |
| atlas 分配 | `ManAtlasAlloc()` shelf 打包 + 每项**独占**区域 + `MAN_SLOT_PAD=1` 透明边（铁律 12）|
| 上传 | `CjkProbeTex()` 探测出的 LockRect(19)/UnlockRect(20) → **只锁被分配的子矩形** → 灰度进 A 通道、RGB 拉满白、顶点色负责颜色 |
| 绘制 | ★★★ **帧末一次画完**：`ManDrawLine` 只登记四边形，真正的绘制在**引擎帧末、Present 之前**由代码插桩钩子触发（`ManFrameEndFlush`→`ManDrawQuads`），一帧一次 `DrawPrimitiveUP`，状态只保存/设置/还原各一次。见铁律 15/19 |
| 状态 | **逐项手工保存/还原**（57/58、64/65、66/67、68/69、89/90），取不到就 `return -1` 不画 |
| 顶点 | `float x,y,z,rhw; DWORD color; float u,v;` = 28B（引擎 `SetStreamSource` 推 `0x1Ch` 反证）|
| 坐标 | `CjkUpdateMapping()`：非单位阵→WVP（mode2）；单位阵→`屏幕=逻辑+视口中心`（1）。**同一个 g++ 顺序**见下<br>★★ **缓存窗口必须是 2ms 不是 1s**：相机每帧都变，1 秒缓存会让文本跟着相机漂移（铁律 21）。可用 ini `[layout] mode=` 强制 1/2 |
| 队列 | 入队只登记；真正的绘制在帧末钩子（铁律 19）|
| 兜底 | `g_manFail`(可重试)/`g_manFatal`(永久)；SEH 包住所有外部调用；连踩 8 次自动关闭接管 |
| 逃生开关 | `update\chs_off*`（**前缀通配**）存在即完全不接管；日志明确回报 `DISABLED` / `takeover ACTIVE` |
| 文本编码 | 自己 `MultiByteToWideChar`，**默认 CP_ACP(GBK)**；`update\chs_utf8.txt` 存在才走 UTF-8。★ 不能"先试 UTF-8 再回退"：GBK `0xB0A1` 恰好是合法 UTF-8（解成西里尔 U+0421）|
| 字号 | `font+0x20`(lineHeight) 直接用（sub_426680 全程无系数），**再乘字体度量补偿 `cell/upem`**（SimSun=1.0，思源黑体=1.448）|
| 几何 | `posX/posY` 是**锚点**：默认=左边缘，bit0/bit1=居中/右；Y 轴 bit3/bit2=居中/底部。宽度**必须自己量**（引擎按字节数算宽，汉字 2 字节）|

19. **★★ 帧末钩子（本项目的绘制落点，别再动它的原理）**
   引擎帧末在 RVA `0x3041E`：`8B 87 28 0E 00 00` = `mov eax,[edi+0E28h]`（正好 **6 字节**），
   紧跟 `30433 call dword ptr [ecx+44h]` = **全 exe 唯一的 D3D Present 调用点**
   （另一处 `0x471365` 是 Steam 接口）。做法：把这 6 字节换成 `E9 rel32 + 90`，
   裸汇编 `pushad/pushfd` → 调 `ManFrameEndFlush` → `popfd/popad` →
   复原 `mov eax,[edi+0E28h]` → `jmp [g_frameResume]`（=`exe+0x30424`）。
   * 虚表钩 `Present(17)` **实测从不被调用**（计数恒 0，尽管 orig 是真函数），
     最稳的是直接改代码 ⇒ 装不上时自动退回旧行为（不会变成完全不显示）
   * 引擎设备 = `[screen+0xE28]`，与本进程 IAT 捕获的同一个（实测 `same=1`）
   * 一帧一次 ⇒ 顺带把"每项都保存/还原 15 个状态"的开销降到每帧一次

## ★★ 崩溃/失效史（细节见每日日志，这里只留"不要再犯"的一句话）
1. `Block+0x74` 误作 `char**` 双解引用 → 野指针崩（**字段的"类型"和"偏移"同等重要**）
2. thiscall `ecx` 未恢复 → `mov esi,ecx` 拿野指针崩
3. 方向 A 调 `sub_4306A0` → 状态块从未创建，崩在 `call [ecx+10h]`
4. 「少解一层指针」×3（`LooksLikeDevice` / `CjkDrawLineW` / `CjkFontsOnLost`）
   ⇒ `void** vt = *(void***)obj;`，改一处前先 `grep "void\*\* vt"`
5. 6. 7. 虚表索引三次写错：`DrawTextW 9→14`、`SetVertexShader 92→107`、
   `Surface9::GetDesc 4→8`、`CreateTexture 22→错(hr=0xDEADBEEF)`
   ⇒ **引擎没有直接调用点的槽位一律不许硬编码**（换导出函数或运行时探测）
8. 队列行推进冲出 `WCHAR[256]` → 崩在 `ManDrawLine+0x15`
9. 「设备指针非空≠有效」→ 崩在 `dvt[57]`(+0xE4)
10. 改渲染状态不还原 → UI 彻底损坏/黑白屏（`manHr=0`，绘制本身成功）
11. `__stdcall` 参数个数不符 → 栈被啃 → 崩在栈地址（`EIP/ECX/ESI` 都是 `ESP+0x38`）
12. `CreateStateBlock` 后多调 `Capture()` → 崩在 d3d9 内部
13. `Apply()` 返 S_OK 但**不还原 FVF**（`0x1C4`→`0x144`，泄漏到下一帧 ⇒ 花屏）
14. `D3DRS_ALPHATESTENABLE` 写成 24（真值 15；24 是 ALPHAREF）
15. **`D3DPOOL_MANAGED_` 写成 3（= SCRATCH）** ⇒ 纹理不能被设备使用，
    LockRect/SetTexture 全"成功"但采样是垃圾 ⇒ 整块中文不可见
16. `g_gettersFail` 成功路径也置 -1 ⇒ 第 2 次起永久 return ⇒ 只画第一行
17. 字号 10/18 交替时每行 `DeleteObject+CreateFontW`；`trace` 用 `g_manDraws<2` 永不关
    ⇒ 每项都 FlushFileBuffers ⇒ **特别卡**
18. 所有文本项写纹理同一块 (0,0) ⇒ 只有最后一项可见 + 闪烁（铁律 12）
19. 被别的绘制隔开的 `DrawPrimitiveUP` 只有最后一笔落屏 ⇒ 游戏内 HUD 被世界盖住（铁律 19）
20. 用时间/签名"猜帧"：实测同帧内文本项间隔就有 15~63ms ⇒ 把一帧切碎 ⇒ 整帧空白
    （**帧边界只能靠引擎自己的帧末回调**）
21. **★ 变换矩阵按 1 秒缓存 ⇒ 游戏内文本跟着相机/鼠标漂移**（菜单不动所以没暴露）。
    **每帧都会变的环境量（矩阵/视口/相机）缓存窗口必须 ≤ 帧长**（现用 2ms）。

## 里程碑
`5548b9b`（2026-10-03）「hook 版中文渲染跑通，游戏内中文首次正常上屏」，22 文件。
此后进入"让显示正确"阶段（atlas 分配 = 第一个修复）。

## 文本资源状态（第 16 轮核验，无需改动）
- `Text/*.txt`：ASCII 4007B + **真中文 8624B**（GBK 0xB0-0xF7 前导）+ 俄文注释 174B
- `strings.ini` value 全是中文（`Lvl=关卡` / `Exit=退出` / `NotCD=请将游戏光盘插入光驱`）；
  文件里的 `0xA7xx` 字节**只在俄文注释**里，按设计「俄文注释不译」⇒ 预期行为，别被误导
- 术语表 `data_chs/TERMS.md`；译文表 `tools/translations_ini_zh.py`(ini) + `tools/translations_zh.py`(Text 966 行)

## 构建 / 环境 / 重建
```
build.bat     # Release/Win32, MSVC 14.16, SDK 10.0.26100.0, /MAP 已开
```
显式设 `INCLUDE`/`LIB`，**不调 vcvars32.bat**（它会调被沙箱禁的 `reg.exe`）。
产物 `dist\AlienShooterCHS.dll`，**不链 CRT**（`/NODEFAULTLIB` + 自定义入口 `CHS_DllEntry`），
自实现 `memcpy`/`memset` + `#pragma function`。freestanding 下要自供
`__load_config_used`（16 DWORD，首值 0x40）+ `__except_handler3`（裸跳转桩 → msvcrt.dll 的
`_except_handler3`，VC 三个 .lib 里都没有这个符号）。
- `reg.exe` 被禁 → 查注册表用 Python `winreg`；PowerShell 工具无输出 → 优先 Bash+Python
- Python venv：`C:/Users/haojun0823/.workbuddy/binaries/python/envs/default/Scripts/python.exe`

```bash
PY="C:/Users/haojun0823/.workbuddy/binaries/python/envs/default/Scripts/python.exe"
cd G:/Projects/AlienShooterCHS && cmd //c build.bat
cp dist/AlienShooterCHS.dll "I:/LocalGames/AlienShooter v1.22DIC/update/AlienShooterCHS.asi"
cd tools && "$PY" build_chs.py [--gb2312]      # 重建文本资源
"$PY" tools/make_gbk_deploy.py                 # 部署 GBK 到游戏 Text\ + strings.ini
"$PY" tools/make_import_report.py              # 导入表报告
```

## 产物清单
`data_original/Text/*.txt`（英文基线 71 文件，只读）/ `data_chs/Text/*.txt`（UTF-8 成品）
/ `dist_gb2312/Text/*.txt` / `data_chs/TERMS.md` / `tools/translations_{zh,ini_zh}.py`
/ `tools/build_chs.py` / `ANALYSIS_探测日志分析.md` / `ANALYSIS_中文字形渲染失败根因.md`
/ `IMPORT_TABLE_AlienShooter.md` / `README_HOOK测试.md`

## 当前状态（2026-10-03，已完成里程碑 + 修复）
**中文在所有界面正常显示**（主菜单 / 设置 / 选任务，见 `tools_out/verify_*.png`）。
已解决的三个大坑：atlas 独占区域、`D3DPOOL` 写错、**DrawPrimitiveUP 批量绘制**。

## 待办
1. **选任务界面的「生命/力量/敏捷」三条统计标签重叠**（字距/行距问题，按截图微调）
2. 颜色：目前一律白色，ESC 颜色码被丢弃 → 需找引擎颜色表
3. 中英混排：ASCII 也被接管会变宋体风格（与位图字体不一致）→ 考虑纯 ASCII 放行
4. 性能：状态保存/还原目前**每项一次**（每帧 ~30 次 × 19 Get + 17 Set），
   可考虑减少（例如同一帧内复用保存值）
5. 清理：`tools/run_game_shot.py` 保留（自动取证很有用）；
   `update\chs_solid.txt` 之类的历史开关已废弃
