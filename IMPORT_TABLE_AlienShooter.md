# Alien Shooter v1.22DIC 导入表（Import Table）

由 `tools/make_import_report.py` 自动生成，数据来源为 pefile 直读 PE 结构，无手工誊写。

## 一、文件标识

| 项 | 值 |
|----|----|
| 路径 | `I:\LocalGames\AlienShooter v1.22DIC\AlienShooter.exe` |
| MD5 | `641f33c3207d37a838d4f7f8f9c40e26` |
| SHA256 | `4dd960458d6fffcc9d00e9e7ba492739fb6d530d4c0b302f1c6baa8b55d9b142` |
| 大小 | 1206272 字节（1.15 MB） |
| 架构 | x86（Machine `0x014C`，32 位 PE32） |
| 子系统 | 2 WINDOWS_GUI |
| ImageBase | `0x00400000` |
| EntryPoint RVA | `0x0007DEAA` |
| 链接器版本 | 14.10 |
| TimeDateStamp | `0x5C5ABEB5` → 2019-02-06 11:02:13 UTC |
| DllCharacteristics | `0x8140`（DYNAMIC_BASE/ASLR + NX_COMPAT/DEP + TERMINAL_SERVER_AWARE） |
| Import Directory RVA | `0x000F697C`（Size `0x104`） |
| 延迟导入表 | 无 |
| 绑定导入表 | 无 |
| CheckSum | `0x00000000`（计算值 `0x00133D26`，不一致—— 非驱动程序，PE 可选） |

> 注：`DllCharacteristics` 含 ASLR/DEP，说明这份二进制是 **2019 年重新编译过**（链接器 14.10 = VS2017 15.5），不是 2008 年 Sigma Team 的原始产物。

### ★ `WINMM.dll` / `DSOUND.dll` 的 sideload 劫持（实测结论）

游戏目录里放着 **Ultimate ASI Loader 9.7.4**（ThirteenAG，MIT），伪装成 **`dsound.dll`**：

| 项 | 值 |
|----|----|
| 文件 | `I:\LocalGames\AlienShooter v1.22DIC\dsound.dll` |
| 真实身份 | Ultimate ASI Loader 9.7.4（ThirteenAG，MIT）|
| 原始名 | `Ultimate-ASI-Loader-Win32.dll` |
| 大小 | 5418336 字节 |
| MD5 | `c4aa7183a88decf99fa695eb8fcdda6c` |
| 导出符号 | **1334 个** |
| 链接器 | 14.51 |

Windows 对非 KnownDLLs **应用目录优先于 System32**，所以 exe 导入
`DSOUND.dll` 时会命中游戏目录里这个 loader。loader 干两件事：

1. **转发** —— 实现全部 1334 个 dsound 导出，游戏调用一切正常
2. **注入** —— 扫描游戏根目录及 `scripts/` `plugins/` `update/` 下的 `*.asi`，逐个 `LoadLibrary`

exe 需要的 DSOUND 符号（按序号导入 #11 = `DirectSoundCreate`）由该 loader 提供。

#### 实测：`winmm.dll` 劫持失败，`dsound.dll` 劫持成功

| DLL | 在导入表 | 在 KnownDLLs | 劫持结果 |
|-----|---------|-------------|---------|
| `WINMM.dll` | 是（6 个函数）| 否 | **失败** |
| `DSOUND.dll` | 是（1 个序号）| 否 | **成功** |

（KnownDLLs 已用注册表核实：38 项里两者都不在，因此都能被 sideload。
winmm 失败的具体原因尚未定位，可能是游戏对 winmm 走了不同的初始化路径。）

> ⚠️ 早期分析曾断言「winmm 劫持是注入链起点」，**该结论错误**，以实测为准。

#### loader 的扫描范围（实测五份副本全部被加载）

```
游戏根目录\*.asi
游戏根目录\*.dll          （Windows 直接加载，绕过 loader）
scripts\*.asi
plugins\*.asi
update\*.asi
```

相关配置键（读 `global.ini`，全部可选，默认即为上述行为）：
`loadfromscriptsonly` / `loadrecursively` / `loadextraplugins` / `dontloadfromdllmain` / `loadfromapi` / `overloadfromfolder` / `globalsets` / `fileloader`

## 二、总览

**12 个 DLL，205 个导入函数。**

| # | DLL | 函数数 | 按序号导入 | IAT 区间 | 用途 |
|---|-----|--------|-----------|----------|------|
| 1 | `WINMM.dll` | 6 | 0 | `0x004D92F8`–`0x004D9310` | 多媒体/音频（sideload 劫持失败，见下） |
| 2 | `d3d9.dll` | 1 | 0 | `0x004D9314`–`0x004D9318` | 图形 |
| 3 | `d3dx9_43.dll` | 2 | 0 | `0x004D931C`–`0x004D9324` | 图形辅助 |
| 4 | `DSOUND.dll` | 1 | 1 | `0x004D9028`–`0x004D902C` | 音频 **← sideload 劫持成功，本项目注入入口，见下** |
| 5 | `KERNEL32.dll` | 111 | 0 | `0x004D9074`–`0x004D9230` | 系统/CRT |
| 6 | `USER32.dll` | 46 | 0 | `0x004D923C`–`0x004D92F4` | 窗口/输入 |
| 7 | `GDI32.dll` | 16 | 0 | `0x004D9030`–`0x004D9070` | 2D 绘制 |
| 8 | `COMDLG32.dll` | 2 | 0 | `0x004D901C`–`0x004D9024` | 对话框 |
| 9 | `ADVAPI32.dll` | 6 | 0 | `0x004D9000`–`0x004D9018` | 注册表 |
| 10 | `SHELL32.dll` | 1 | 0 | `0x004D9234`–`0x004D9238` | Shell |
| 11 | `ole32.dll` | 3 | 0 | `0x004D9328`–`0x004D9334` | COM |
| 12 | `steam_api.dll` | 10 | 0 | `0x004D9338`–`0x004D9360` | Steamworks |
| | **合计** | **205** | **1** | | |

## 三、逐 DLL 明细

### WINMM.dll

多媒体/音频（sideload 劫持失败，见下），函数 6 个。

> mciSendStringA 用于 MCI 播放 wav/mp3；timeBeginPeriod/timeEndPeriod 提高定时器精度（固定步长主循环）；timeGetTime 取全局毫秒

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D92F8` | — | `auxSetVolume` |
| `0x004D92FC` | — | `waveOutSetVolume` |
| `0x004D9300` | — | `mciSendStringA` |
| `0x004D9304` | — | `timeEndPeriod` |
| `0x004D9308` | — | `timeBeginPeriod` |
| `0x004D930C` | — | `timeGetTime` |

### d3d9.dll

图形，函数 1 个。

> 唯一渲染后端：Direct3DCreate9 建立 D3D9 设备

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D9314` | — | `Direct3DCreate9` |

### d3dx9_43.dll

图形辅助，函数 2 个。

> D3DX43 扩展：仅从内存/表面加载纹理，未用 D3DX 字体接口

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D931C` | — | `D3DXLoadSurfaceFromMemory` |
| `0x004D9320` | — | `D3DXLoadSurfaceFromSurface` |

### DSOUND.dll

音频 **← sideload 劫持成功，本项目注入入口，见下**，函数 1 个。

> DirectSound，按序号 #11 导入 = DirectSoundCreate

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D9028` | 11 | — |

### KERNEL32.dll

系统/CRT，函数 111 个。

> 文件、INI、堆、TLS、编码转换、命令行、异常展开 —— 绝大部分是 MSVC CRT 静态库带入

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D9074` | — | `GetDateFormatA` |
| `0x004D9078` | — | `GetTimeFormatA` |
| `0x004D907C` | — | `MultiByteToWideChar` |
| `0x004D9080` | — | `OutputDebugStringA` |
| `0x004D9084` | — | `GetEnvironmentStringsW` |
| `0x004D9088` | — | `GetCPInfo` |
| `0x004D908C` | — | `GetOEMCP` |
| `0x004D9090` | — | `IsValidCodePage` |
| `0x004D9094` | — | `FindNextFileW` |
| `0x004D9098` | — | `FindNextFileA` |
| `0x004D909C` | — | `FindFirstFileExW` |
| `0x004D90A0` | — | `FindFirstFileExA` |
| `0x004D90A4` | — | `FindClose` |
| `0x004D90A8` | — | `GetFileAttributesExW` |
| `0x004D90AC` | — | `SetStdHandle` |
| `0x004D90B0` | — | `GetTimeZoneInformation` |
| `0x004D90B4` | — | `FlushFileBuffers` |
| `0x004D90B8` | — | `MoveFileExW` |
| `0x004D90BC` | — | `DeleteFileW` |
| `0x004D90C0` | — | `CreateDirectoryW` |
| `0x004D90C4` | — | `SetFilePointerEx` |
| `0x004D90C8` | — | `GetStringTypeW` |
| `0x004D90CC` | — | `GetConsoleCP` |
| `0x004D90D0` | — | `FileTimeToSystemTime` |
| `0x004D90D4` | — | `GetConsoleMode` |
| `0x004D90D8` | — | `GetFileType` |
| `0x004D90DC` | — | `EnumSystemLocalesW` |
| `0x004D90E0` | — | `GetUserDefaultLCID` |
| `0x004D90E4` | — | `IsValidLocale` |
| `0x004D90E8` | — | `GetLocaleInfoW` |
| `0x004D90EC` | — | `LCMapStringW` |
| `0x004D90F0` | — | `CompareStringW` |
| `0x004D90F4` | — | `GetTimeFormatW` |
| `0x004D90F8` | — | `GetDateFormatW` |
| `0x004D90FC` | — | `DecodePointer` |
| `0x004D9100` | — | `GetCurrentThread` |
| `0x004D9104` | — | `HeapAlloc` |
| `0x004D9108` | — | `HeapFree` |
| `0x004D910C` | — | `GetACP` |
| `0x004D9110` | — | `WriteFile` |
| `0x004D9114` | — | `GetStdHandle` |
| `0x004D9118` | — | `GetModuleFileNameW` |
| `0x004D911C` | — | `GetModuleFileNameA` |
| `0x004D9120` | — | `WideCharToMultiByte` |
| `0x004D9124` | — | `ReadFile` |
| `0x004D9128` | — | `GetCommandLineW` |
| `0x004D912C` | — | `GetCommandLineA` |
| `0x004D9130` | — | `GetModuleHandleExW` |
| `0x004D9134` | — | `ExitProcess` |
| `0x004D9138` | — | `SetLastError` |
| `0x004D913C` | — | `InterlockedFlushSList` |
| `0x004D9140` | — | `GetPrivateProfileSectionA` |
| `0x004D9144` | — | `GetPrivateProfileStringA` |
| `0x004D9148` | — | `GetPrivateProfileIntA` |
| `0x004D914C` | — | `GlobalLock` |
| `0x004D9150` | — | `GlobalUnlock` |
| `0x004D9154` | — | `GlobalSize` |
| `0x004D9158` | — | `GlobalAlloc` |
| `0x004D915C` | — | `CloseHandle` |
| `0x004D9160` | — | `GetFileTime` |
| `0x004D9164` | — | `FileTimeToLocalFileTime` |
| `0x004D9168` | — | `CreateFileA` |
| `0x004D916C` | — | `GetCurrentDirectoryA` |
| `0x004D9170` | — | `Sleep` |
| `0x004D9174` | — | `MulDiv` |
| `0x004D9178` | — | `FreeEnvironmentStringsW` |
| `0x004D917C` | — | `SetEnvironmentVariableW` |
| `0x004D9180` | — | `SetEnvironmentVariableA` |
| `0x004D9184` | — | `GetProcessHeap` |
| `0x004D9188` | — | `OutputDebugStringW` |
| `0x004D918C` | — | `SetConsoleCtrlHandler` |
| `0x004D9190` | — | `HeapReAlloc` |
| `0x004D9194` | — | `CreateFileW` |
| `0x004D9198` | — | `GetTempPathW` |
| `0x004D919C` | — | `HeapSize` |
| `0x004D91A0` | — | `WriteConsoleW` |
| `0x004D91A4` | — | `ReadConsoleW` |
| `0x004D91A8` | — | `SetEndOfFile` |
| `0x004D91AC` | — | `QueryPerformanceCounter` |
| `0x004D91B0` | — | `GetStartupInfoW` |
| `0x004D91B4` | — | `InterlockedPushEntrySList` |
| `0x004D91B8` | — | `RaiseException` |
| `0x004D91BC` | — | `LoadLibraryExW` |
| `0x004D91C0` | — | `FreeLibrary` |
| `0x004D91C4` | — | `TlsFree` |
| `0x004D91C8` | — | `TlsSetValue` |
| `0x004D91CC` | — | `TlsGetValue` |
| `0x004D91D0` | — | `TlsAlloc` |
| `0x004D91D4` | — | `InitializeCriticalSectionAndSpinCount` |
| `0x004D91D8` | — | `GetLastError` |
| `0x004D91DC` | — | `EncodePointer` |
| `0x004D91E0` | — | `RtlUnwind` |
| `0x004D91E4` | — | `InitializeSListHead` |
| `0x004D91E8` | — | `GetSystemTimeAsFileTime` |
| `0x004D91EC` | — | `GetCurrentThreadId` |
| `0x004D91F0` | — | `EnterCriticalSection` |
| `0x004D91F4` | — | `LeaveCriticalSection` |
| `0x004D91F8` | — | `DeleteCriticalSection` |
| `0x004D91FC` | — | `SetEvent` |
| `0x004D9200` | — | `ResetEvent` |
| `0x004D9204` | — | `WaitForSingleObjectEx` |
| `0x004D9208` | — | `CreateEventW` |
| `0x004D920C` | — | `GetModuleHandleW` |
| `0x004D9210` | — | `GetProcAddress` |
| `0x004D9214` | — | `UnhandledExceptionFilter` |
| `0x004D9218` | — | `SetUnhandledExceptionFilter` |
| `0x004D921C` | — | `GetCurrentProcess` |
| `0x004D9220` | — | `TerminateProcess` |
| `0x004D9224` | — | `IsProcessorFeaturePresent` |
| `0x004D9228` | — | `IsDebuggerPresent` |
| `0x004D922C` | — | `GetCurrentProcessId` |

### USER32.dll

窗口/输入，函数 46 个。

> 窗口创建、消息循环、菜单、剪贴板、鼠标捕获、加速键、对话框

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D923C` | — | `PeekMessageA` |
| `0x004D9240` | — | `GetWindowRect` |
| `0x004D9244` | — | `SetCursorPos` |
| `0x004D9248` | — | `ClientToScreen` |
| `0x004D924C` | — | `SetWindowPos` |
| `0x004D9250` | — | `GetDC` |
| `0x004D9254` | — | `ReleaseDC` |
| `0x004D9258` | — | `GetDlgItem` |
| `0x004D925C` | — | `SendDlgItemMessageA` |
| `0x004D9260` | — | `SetCapture` |
| `0x004D9264` | — | `ReleaseCapture` |
| `0x004D9268` | — | `EnableWindow` |
| `0x004D926C` | — | `GetSystemMetrics` |
| `0x004D9270` | — | `DrawMenuBar` |
| `0x004D9274` | — | `RedrawWindow` |
| `0x004D9278` | — | `EmptyClipboard` |
| `0x004D927C` | — | `GetClipboardData` |
| `0x004D9280` | — | `SetClipboardData` |
| `0x004D9284` | — | `CloseClipboard` |
| `0x004D9288` | — | `OpenClipboard` |
| `0x004D928C` | — | `MessageBoxA` |
| `0x004D9290` | — | `GetForegroundWindow` |
| `0x004D9294` | — | `DestroyCursor` |
| `0x004D9298` | — | `LoadCursorFromFileA` |
| `0x004D929C` | — | `LoadCursorA` |
| `0x004D92A0` | — | `WaitMessage` |
| `0x004D92A4` | — | `LoadIconA` |
| `0x004D92A8` | — | `SetCursor` |
| `0x004D92AC` | — | `ShowCursor` |
| `0x004D92B0` | — | `UpdateWindow` |
| `0x004D92B4` | — | `TranslateAcceleratorA` |
| `0x004D92B8` | — | `LoadAcceleratorsA` |
| `0x004D92BC` | — | `DialogBoxParamA` |
| `0x004D92C0` | — | `CreateWindowExA` |
| `0x004D92C4` | — | `RegisterClassA` |
| `0x004D92C8` | — | `PostQuitMessage` |
| `0x004D92CC` | — | `PostMessageA` |
| `0x004D92D0` | — | `DispatchMessageA` |
| `0x004D92D4` | — | `TranslateMessage` |
| `0x004D92D8` | — | `SetWindowTextA` |
| `0x004D92DC` | — | `GetDlgItemTextA` |
| `0x004D92E0` | — | `SetDlgItemTextA` |
| `0x004D92E4` | — | `EndDialog` |
| `0x004D92E8` | — | `ShowWindow` |
| `0x004D92EC` | — | `DefWindowProcA` |
| `0x004D92F0` | — | `GetClientRect` |

### GDI32.dll

2D 绘制，函数 16 个。

> CreateFontA + ExtTextOutA + GetTextExtentPoint32A —— GDI 文本路径，另有 DIB section blit

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D9030` | — | `SetPixel` |
| `0x004D9034` | — | `GetPixel` |
| `0x004D9038` | — | `ExtTextOutA` |
| `0x004D903C` | — | `CreateDIBSection` |
| `0x004D9040` | — | `SetTextAlign` |
| `0x004D9044` | — | `SetTextColor` |
| `0x004D9048` | — | `SetMapMode` |
| `0x004D904C` | — | `SetBkColor` |
| `0x004D9050` | — | `SelectObject` |
| `0x004D9054` | — | `GetTextExtentPoint32A` |
| `0x004D9058` | — | `GetDeviceCaps` |
| `0x004D905C` | — | `DeleteObject` |
| `0x004D9060` | — | `DeleteDC` |
| `0x004D9064` | — | `CreateFontA` |
| `0x004D9068` | — | `CreateCompatibleDC` |
| `0x004D906C` | — | `GetStockObject` |

### COMDLG32.dll

对话框，函数 2 个。

> GetOpenFileNameA / GetSaveFileNameA 通用打开/保存对话框

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D901C` | — | `GetOpenFileNameA` |
| `0x004D9020` | — | `GetSaveFileNameA` |

### ADVAPI32.dll

注册表，函数 6 个。

> RegOpenKeyExA/RegQueryValueExA 等 —— 游戏设置项持久化（键值均为 ANSI）

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D9000` | — | `RegQueryValueExA` |
| `0x004D9004` | — | `RegOpenKeyExA` |
| `0x004D9008` | — | `RegDeleteValueA` |
| `0x004D900C` | — | `RegCreateKeyExA` |
| `0x004D9010` | — | `RegCloseKey` |
| `0x004D9014` | — | `RegSetValueExA` |

### SHELL32.dll

Shell，函数 1 个。

> ShellExecuteA —— 启动外部程序/打开链接

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D9234` | — | `ShellExecuteA` |

### ole32.dll

COM，函数 3 个。

> CoInitialize / CoCreateInstance / CoUninitialize

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D9328` | — | `CoCreateInstance` |
| `0x004D932C` | — | `CoUninitialize` |
| `0x004D9330` | — | `CoInitialize` |

### steam_api.dll

Steamworks，函数 10 个。

> SteamAPI_Init 等 10 个导出 —— Steam 平台接口，非系统 DLL，由 Steam 客户端提供

| IAT 地址 | 序号 | 名称 |
|----------|------|------|
| `0x004D9338` | — | `SteamAPI_RunCallbacks` |
| `0x004D933C` | — | `SteamAPI_RestartAppIfNecessary` |
| `0x004D9340` | — | `SteamAPI_Shutdown` |
| `0x004D9344` | — | `SteamAPI_Init` |
| `0x004D9348` | — | `SteamAPI_RegisterCallResult` |
| `0x004D934C` | — | `SteamAPI_UnregisterCallResult` |
| `0x004D9350` | — | `SteamAPI_GetHSteamPipe` |
| `0x004D9354` | — | `SteamAPI_GetHSteamUser` |
| `0x004D9358` | — | `SteamInternal_ContextInit` |
| `0x004D935C` | — | `SteamInternal_CreateInterface` |

## 四、按功能归类

### 4.1 图形 / 渲染

| DLL | 函数 | 说明 |
|-----|------|------|
| d3d9.dll | `Direct3DCreate9` | 唯一的 D3D 设备创建入口，其余 D3D9 接口靠 `IDirect3D9` 虚表与 COM 在运行时取得，不进 IAT |
| d3dx9_43.dll | `D3DXLoadSurfaceFromMemory` / `D3DXLoadSurfaceFromSurface` | 只做纹理解码，**没有** `D3DXCreateFont` 之类 |
| GDI32.dll | `CreateFontA` / `ExtTextOutA` / `GetTextExtentPoint32A` | 唯一可能的 GDI 文本通路（对应分析报告的「方向 C」）|
| GDI32.dll | `CreateDIBSection` / `CreateCompatibleDC` / `SelectObject` / `GetPixel` / `SetPixel` / `GetDeviceCaps` / `DeleteDC` / `DeleteObject` / `GetStockObject` | DIB section 内存 blit，像素级读写（D3D 互操作或后处理）|
| GDI32.dll | `SetTextColor` / `SetBkColor` / `SetTextAlign` / `SetMapMode` | 文本与坐标模式设置 |

### 4.2 音频

| DLL | 函数 | 说明 |
|-----|------|------|
| WINMM.dll | `mciSendStringA` | MCI 字符串接口播放音乐/音效 |
| WINMM.dll | `waveOutSetVolume` / `auxSetVolume` | 音量控制 |
| WINMM.dll | `timeBeginPeriod` / `timeEndPeriod` | 提升定时器精度 —— **证明主循环是固定时间步进** |
| WINMM.dll | `timeGetTime` | 全局毫秒计时，游戏帧计时基准 |
| DSOUND.dll | 序号 #11 = `DirectSoundCreate` | DirectSound 设备创建（音效）|

### 4.3 文件与配置

| DLL | 函数 | 说明 |
|-----|------|------|
| KERNEL32.dll | `GetPrivateProfileStringA` / `GetPrivateProfileIntA` / `GetPrivateProfileSectionA` | **INI 配置读取，全 ANSI** —— 游戏设置走 `strings.ini` 这类文件 |
| KERNEL32.dll | `CreateFileA` / `CreateFileW` / `ReadFile` / `WriteFile` / `SetFilePointerEx` / `GetFileSize` 类 | 双版本文件 IO（A + W 都有，说明有 UTF-16 路径处理）|
| KERNEL32.dll | `GetFileAttributesExW` / `FindFirstFileExA` / `FindFirstFileExW` / `FindNextFileA` / `FindNextFileW` / `FindClose` | 目录枚举 |
| KERNEL32.dll | `CreateDirectoryW` / `DeleteFileW` / `MoveFileExW` | 文件操作（W 版）|
| KERNEL32.dll | `GetTempPathW` | 临时目录 |
| ADVAPI32.dll | `RegOpenKeyExA` / `RegQueryValueExA` / `RegSetValueExA` / `RegCreateKeyExA` / `RegDeleteValueA` / `RegCloseKey` | 注册表持久化，**全 A 版** |
| COMDLG32.dll | `GetOpenFileNameA` / `GetSaveFileNameA` | 通用打开/保存对话框（ANSI）|

### 4.4 窗口与输入

| DLL | 函数组 | 说明 |
|-----|--------|------|
| USER32.dll | `RegisterClassA` / `CreateWindowExA` / `DefWindowProcA` / `ShowWindow` / `UpdateWindow` | 窗口创建（ANSI 版，无 W）|
| USER32.dll | `PeekMessageA` / `WaitMessage` / `TranslateMessage` / `TranslateAcceleratorA` / `DispatchMessageA` / `PostQuitMessage` | 消息循环 |
| USER32.dll | `PostMessageA` / `SendDlgItemMessageA` / `SetCapture` / `ReleaseCapture` | 窗口间通信与鼠标捕获 |
| USER32.dll | `DialogBoxParamA` / `EndDialog` / `GetDlgItem` / `GetDlgItemTextA` / `SetDlgItemTextA` / `SetDlgItemTextA` | 对话框与文本控件 |
| USER32.dll | `LoadAcceleratorsA` / `DrawMenuBar` | 菜单与加速键 |
| USER32.dll | `LoadCursorA` / `LoadCursorFromFileA` / `SetCursor` / `ShowCursor` / `DestroyCursor` / `SetCursorPos` / `ClientToScreen` / `GetSystemMetrics` / `GetForegroundWindow` | 自绘光标 + 鼠标定位（FPS 视角控制）|
| USER32.dll | `OpenClipboard` / `CloseClipboard` / `EmptyClipboard` / `SetClipboardData` / `GetClipboardData` | 剪贴板（复制/粘贴作弊码或序列）|
| USER32.dll | `MessageBoxA` / `GetWindowRect` / `SetWindowPos` / `RedrawWindow` / `GetDC` / `ReleaseDC` / `GetClientRect` / `SetWindowTextA` | 窗口杂项 |

### 4.5 编码转换

| 函数 | 说明 |
|------|------|
| `MultiByteToWideChar` | 多字节 → UTF-16 |
| `WideCharToMultiByte` | UTF-16 → 多字节 |
| `GetACP` / `GetOEMCP` / `GetCPInfo` / `IsValidCodePage` | 代码页查询 |
| `LCMapStringW` / `CompareStringW` / `GetLocaleInfoW` / `IsValidLocale` / `GetUserDefaultLCID` | CRT 本地化 |
| `GetStringTypeW` / `GetDateFormatA` / `GetTimeFormatA` / `GetDateFormatW` / `GetTimeFormatW` | CRT 日期时间格式化 |

> **与中文字形渲染相关的关键判断**：这些 `*W` 系列函数全部来自 MSVC CRT（`__crtGetLocaleInfo` 一类内部调用），引擎自身的文本绘制主循环（`0x426680`）是逐字节的，没有调用它们。
> 因此**不能**通过在 CRT 层拦截转码来注入 CJK 支持 —— 引擎根本不过 CRT 转码路径。

### 4.6 CRT / 运行时

堆：`HeapAlloc` / `HeapFree` / `HeapReAlloc` / `HeapSize` / `GetProcessHeap` / `GlobalAlloc` / `GlobalLock` / `GlobalUnlock` / `GlobalSize`；

TLS：`TlsAlloc` / `TlsFree` / `TlsGetValue` / `TlsSetValue`；

同步：`EnterCriticalSection` / `LeaveCriticalSection` / `DeleteCriticalSection` / `InitializeCriticalSectionAndSpinCount` / `InterlockedFlushSList` / `InterlockedPushEntrySList` / `InitializeSListHead`；

异常：`RaiseException` / `RtlUnwind`；

编码标记：`EncodePointer` / `DecodePointer`（CRT 安全 cookie 支持）；

控制台：`SetStdHandle` / `GetStdHandle` / `WriteConsoleW` / `ReadConsoleW` / `SetConsoleCtrlHandler` / `GetConsoleMode` / `GetConsoleCP`；

环境/命令行：`GetCommandLineA` / `GetCommandLineW` / `GetEnvironmentStringsW` / `SetEnvironmentVariableA` / `SetEnvironmentVariableW` / `FreeEnvironmentStringsW`；

模块/进程：`GetModuleHandleExW` / `GetModuleFileNameA` / `GetModuleFileNameW` / `LoadLibraryExW` / `FreeLibrary` / `ExitProcess` / `Sleep`；

动态加载：`GetModuleHandleExW` + `LoadLibraryExW` 表明运行时还会 **动态** 载入额外 DLL —— 静态导入表看不到全部依赖。

### 4.7 Steamworks

| 函数 | 说明 |
|------|------|
| `SteamAPI_Init` / `SteamAPI_Shutdown` | 初始化/关闭 |
| `SteamAPI_RunCallbacks` / `SteamAPI_RestartAppIfNecessary` | 回调与启动检查 |
| `SteamAPI_GetHSteamPipe` / `SteamAPI_GetHSteamUser` | 内部管道句柄 |
| `SteamAPI_RegisterCallResult` / `SteamAPI_UnregisterCallResult` | 异步调用结果注册 |
| `SteamInternal_ContextInit` / `SteamInternal_CreateInterface` | 内部上下文与接口工厂 |

> `steam_api.dll` 是**非系统 DLL**，必须由 Steam 客户端提供。若未安装 Steam 直接跑 exe，会因加载该 DLL 失败而在进程启动阶段就崩溃（导入表在 IAT 填充时就已解析全部 DLL，无法延迟）。
> —— 这也是本项目注入式 Hook 必须保留 `steam_api.dll` 可解析的原因。

## 五、逆向要点小结

| 观察 | 结论 |
|------|------|
| 全部 UI API 均为 A 版（ANSI），无 `*W` 版窗口/文本接口 | 引擎内部字符串以单字节 ANSI 为准，与 `0x426680` 逐字节绘制循环一致 |
| CRT 的 `*W` 转码函数在导入表里，但引擎不经过它们 | 无法在 CRT 层打补丁注入 CJK |
| 未导入任何字体 API（GDI 字体只有 `CreateFontA`） | 无 TrueType/Unicode 字体参与渲染，字体全靠自建 TGA 图集 |
| `timeBeginPeriod` + `timeGetTime` | 主循环固定步长，与 hook 时序逻辑相关 |
| `Direct3DCreate9` 是唯一图形入口 | D3D9 虚表调用，hook 需落在具体方法而非导入 thunk |
| 有 `LoadLibraryExW` | 存在运行时动态依赖，静态导入表 ≠ 完整依赖清单 |

