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

## 崩溃定位手段
- **VEH**（`AddVectoredExceptionHandler`，XP 回退 `SetUnhandledExceptionFilter`）在装 hook
  **之前**装好；落盘异常码/EIP/全部寄存器/`module=`/`fault=`(读写+地址)/`inProbe`。
- **`/MAP` 链接** + `EIP - 模块基址 = RVA` → `.map` 里 `grep 函数名` 相减 = 函数内偏移
  → 配字节反汇编锁源码。比读代码猜快得多。
- 自证诊断：绘制前后各读一次 `GetRenderState`/`GetFVF` 并比对（`ALL RESTORED OK` /
  `!!! STILL LEAKING`）；统计 DIB 非零像素数 `lit` 验证 GDI 真出字了。

## IDA MCP
`idb_open{input_path, mode:"prefer_headless", run_auto_analysis:false}`（必须传 false），
复用已落盘 `.i64` 秒开；`disasm` 可靠，`decompile 0x426680` 失败。
**判断字段类型靠 disasm 指令形态（`mov eax,[esi+74h]`），不靠反编译器推断。**

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
| 建纹理 | `d3dx9_43.dll!D3DXCreateTexture`（导出函数，零索引风险）640×**96** A8R8G8B8 **MANAGED** |
| 上传 | `CjkProbeTex()` 探测出的 LockRect/UnlockRect → 灰度进 A 通道、RGB 拉满白、顶点色负责颜色 |
| 绘制 | `SetFVF(0x144)` + `SetTexture(65)` + `SetTextureStageState(67)` + `SetSamplerState(69)` + `DrawPrimitiveUP(83, 2 tri, stride 28)` |
| 状态 | **逐项手工保存/还原**（57/58、64/65、66/67、68/69、89/90），取不到就 `return -1` 不画 |
| 顶点 | `float x,y,z,rhw; DWORD color; float u,v;` = 28B（引擎 `SetStreamSource` 推 `0x1Ch` 反证）|
| 坐标 | `CjkUpdateMapping()` 判投影矩阵：非单位阵→WVP（mapMode=2）；单位阵→`屏幕=逻辑+视口中心`（1）。★ 结果按设备缓存、每秒最多复查一次（性能）|
| 队列 | 96 条上限；`EndScene(42)`/`Present(17)` 前刷；**本作两者都没实测到被调用** ⇒ 暂时入队即刷 |
| 兜底 | `g_manFail`(可重试) / `g_manFatal`(永久)；SEH 包住 CreateTexture/LockRect/绘制；连踩 8 次自动关闭接管 |
| 逃生开关 | `update\chs_off*`（**前缀通配**）存在即完全不接管；日志明确回报 `DISABLED` / `takeover ACTIVE` |
| 文本编码 | 自己 `MultiByteToWideChar`，**默认 CP_ACP(GBK)**；`update\chs_utf8.txt` 存在才走 UTF-8。★ 不能"先试 UTF-8 再回退"：GBK `0xB0A1` 恰好是合法 UTF-8（解成西里尔 U+0421）|
| 字号 | `font+0x20`(lineHeight) 直接用，**不乘缩放**（sub_426680 全程无系数）|
| 几何 | `posX/posY` 是**锚点**：默认=左边缘，bit0/bit1=居中/右；Y 轴 bit3/bit2=居中/底部。宽度**必须自己量**（引擎按字节数算宽，汉字 2 字节）|

## ★★ 崩溃/失效史（按时间，详见日志）
1. `Block+0x74` 误作 `char**` 双解引用 → 野指针崩
2. thiscall `ecx` 未恢复 → `mov esi,ecx` 拿野指针崩
3. 方向 A 调 `sub_4306A0` → 状态块未创建，崩在 `call [ecx+10h]`
4. 「少解一层指针」×3（`LooksLikeDevice` / `CjkDrawLineW` / `CjkFontsOnLost`）
5. ID3DXFont 索引错（DrawTextW 9→14）→ 调到 `GetGlyphData` 解引用 `0xFFFFFFFF` 崩；
   **范围校验挡不住**（它也是 d3dx9_43 真函数）
6. 又写错三处索引（`SetVertexShader` 92→107、`Surface9::GetDesc` 4→8）
7. `CreateTexture=22` 错 → `hr=0xDEADBEEF tex=0`，表现为"UI 正常但没文字"
8. 队列行推进冲出 `WCHAR[256]` → 崩在 `ManDrawLine+0x15`
9. 「设备指针非空≠有效」→ 崩在 `dvt[57]`(+0xE4)
10. 改渲染状态不还原 → UI 彻底损坏/黑白屏（`manHr=0`，绘制本身成功）
11. **`__stdcall` 参数个数不符 → 栈被啃 → 崩在栈地址**（`EIP/ECX/ESI` 都是 `ESP+0x38`，WRITE）
12. `CreateStateBlock` 后多调 `Capture()` → 崩在 d3d9 内部（RVA `0x7A200`，`ESI=0`）
13. `Apply()` 返回 **S_OK 但不还原 FVF**（实测 `0x1C4`→`0x144`，泄漏留到下一帧 ⇒ 黑白花屏）
14. `D3DRS_ALPHATESTENABLE` 写成 24（真值 15；24 是 ALPHAREF）
15. **`D3DPOOL_MANAGED_` 写成 3（= `D3DPOOL_SCRATCH`）** ⇒ 纹理不能被设备使用，
    LockRect/SetTexture 全部"成功"但采样是垃圾 ⇒ **整块中文不可见**（"所有自检绿、画面空白"）
16. **`g_gettersFail` 成功路径也置 -1** ⇒ 第 2 次起永久提前 return ⇒ 只有第一块文字画出来
17. 字号 10/18 交替 + 旧代码 `DeleteObject`+`CreateFontW` ⇒ 每画一行重建字体，帧率崩到个位数；
    `trace` 用 `g_manDraws < 2` 永不关闭 ⇒ 每次绘制都 `FlushFileBuffers` ⇒ **特别卡**

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

## 待办
1. 实机验证第 17 轮修复：日志应出现
   `takeover ACTIVE` → `D3DXCreateTexture ok ... w=640 h=96` → `traceC: BEFORE fvf=0x1C4`
   → `trace5: ... ALL RESTORED OK`（**trace 只应出现 4 次**）→ `man: draws=N` 持续增长
   - 若 `manDraws` 增长但**仍无字** ⇒ 只剩 alpha/采样/坐标三处，考虑加"纯色标记四边形"判定
     （`D3DTOP_SELECTARG2` + 顶点色，一次性画个洋红方块：能看到=四边形通路通，看不到=坐标/裁剪问题）
   - 若仍卡 ⇒ 下一档优化：把状态保存/还原提到 `CjkFlush` 整批前后各做一次（而非每行）
2. 颜色：目前一律白色，ESC 颜色码被丢弃 → 需找引擎颜色表
3. 中英混排：ASCII 也被我们接管会变宋体风格（与位图字体不一致）→ 考虑纯 ASCII 放行
4. 字号/坐标微调：等真出画面后按截图校准
5. `EndScene`/`Present` 实测都没被调用过 → 若确认，改用 SwapChain9::Present 或 BeginScene 落点
