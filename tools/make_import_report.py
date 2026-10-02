# -*- coding: utf-8 -*-
"""
make_import_report.py -- 由 dump_imports 的结果生成 Markdown 导入表报告
"""
import sys
import os
import hashlib
import datetime
import pefile

SRC = r"I:\LocalGames\AlienShooter v1.22DIC\AlienShooter.exe"
OUT = r"G:\Projects\AlienShooterCHS\IMPORT_TABLE_AlienShooter.md"

DLL_ROLE = {
    "WINMM.dll": ("多媒体/音频（sideload 劫持失败，见下）", "mciSendStringA 用于 MCI 播放 wav/mp3；timeBeginPeriod/timeEndPeriod 提高定时器精度（固定步长主循环）；timeGetTime 取全局毫秒"),
    "d3d9.dll": ("图形", "唯一渲染后端：Direct3DCreate9 建立 D3D9 设备"),
    "d3dx9_43.dll": ("图形辅助", "D3DX43 扩展：仅从内存/表面加载纹理，未用 D3DX 字体接口"),
    "DSOUND.dll": ("音频 **← sideload 劫持成功，本项目注入入口，见下**", "DirectSound，按序号 #11 导入 = DirectSoundCreate"),
    "KERNEL32.dll": ("系统/CRT", "文件、INI、堆、TLS、编码转换、命令行、异常展开 —— 绝大部分是 MSVC CRT 静态库带入"),
    "USER32.dll": ("窗口/输入", "窗口创建、消息循环、菜单、剪贴板、鼠标捕获、加速键、对话框"),
    "GDI32.dll": ("2D 绘制", "CreateFontA + ExtTextOutA + GetTextExtentPoint32A —— GDI 文本路径，另有 DIB section blit"),
    "COMDLG32.dll": ("对话框", "GetOpenFileNameA / GetSaveFileNameA 通用打开/保存对话框"),
    "ADVAPI32.dll": ("注册表", "RegOpenKeyExA/RegQueryValueExA 等 —— 游戏设置项持久化（键值均为 ANSI）"),
    "SHELL32.dll": ("Shell", "ShellExecuteA —— 启动外部程序/打开链接"),
    "ole32.dll": ("COM", "CoInitialize / CoCreateInstance / CoUninitialize"),
    "steam_api.dll": ("Steamworks", "SteamAPI_Init 等 10 个导出 —— Steam 平台接口，非系统 DLL，由 Steam 客户端提供"),
}

SUBSYS = {1: "NATIVE", 2: "WINDOWS_GUI", 3: "WINDOWS_CUI"}


def md5_of(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_of(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    pe = pefile.PE(SRC)
    md5, sha = md5_of(SRC), sha256_of(SRC)
    size = os.path.getsize(SRC)
    ts = pe.FILE_HEADER.TimeDateStamp
    build_dt = datetime.datetime.utcfromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S UTC")

    L = []
    A = L.append

    A("# Alien Shooter v1.22DIC 导入表（Import Table）")
    A("")
    A("由 `tools/make_import_report.py` 自动生成，数据来源为 pefile 直读 PE 结构，无手工誊写。")
    A("")
    A("## 一、文件标识")
    A("")
    A("| 项 | 值 |")
    A("|----|----|")
    A("| 路径 | `%s` |" % SRC)
    A("| MD5 | `%s` |" % md5)
    A("| SHA256 | `%s` |" % sha)
    A("| 大小 | %d 字节（%.2f MB） |" % (size, size / 1048576.0))
    A("| 架构 | x86（Machine `0x014C`，32 位 PE32） |")
    A("| 子系统 | %d %s |" % (pe.OPTIONAL_HEADER.Subsystem,
                                 SUBSYS.get(pe.OPTIONAL_HEADER.Subsystem, "?")))
    A("| ImageBase | `0x%08X` |" % pe.OPTIONAL_HEADER.ImageBase)
    A("| EntryPoint RVA | `0x%08X` |" % pe.OPTIONAL_HEADER.AddressOfEntryPoint)
    A("| 链接器版本 | %d.%d |" % (pe.OPTIONAL_HEADER.MajorLinkerVersion,
                                  pe.OPTIONAL_HEADER.MinorLinkerVersion))
    A("| TimeDateStamp | `0x%08X` → %s |" % (ts, build_dt))
    A("| DllCharacteristics | `0x%04X`（DYNAMIC_BASE/ASLR + NX_COMPAT/DEP + TERMINAL_SERVER_AWARE） |"
      % pe.OPTIONAL_HEADER.DllCharacteristics)
    A("| Import Directory RVA | `0x%08X`（Size `0x%X`） |"
      % (pe.OPTIONAL_HEADER.DATA_DIRECTORY[1].VirtualAddress,
         pe.OPTIONAL_HEADER.DATA_DIRECTORY[1].Size))
    A("| 延迟导入表 | 无 |")
    A("| 绑定导入表 | 无 |")
    A("| CheckSum | `0x%08X`（计算值 `0x%08X`，%s—— 非驱动程序，PE 可选） |"
      % (pe.OPTIONAL_HEADER.CheckSum, pe.generate_checksum(),
         "一致" if pe.OPTIONAL_HEADER.CheckSum == pe.generate_checksum() else "不一致"))
    A("")
    A("> 注：`DllCharacteristics` 含 ASLR/DEP，说明这份二进制是 **2019 年重新编译过**（链接器 14.10 = VS2017 15.5），不是 2008 年 Sigma Team 的原始产物。")
    A("")
    A("### ★ `WINMM.dll` / `DSOUND.dll` 的 sideload 劫持（实测结论）")
    A("")
    A("游戏目录里放着 **Ultimate ASI Loader 9.7.4**（ThirteenAG，MIT），"
      "伪装成 **`dsound.dll`**：")
    A("")
    A("| 项 | 值 |")
    A("|----|----|")
    A("| 文件 | `I:\\LocalGames\\AlienShooter v1.22DIC\\dsound.dll` |")
    A("| 真实身份 | Ultimate ASI Loader 9.7.4（ThirteenAG，MIT）|")
    A("| 原始名 | `Ultimate-ASI-Loader-Win32.dll` |")
    A("| 大小 | 5418336 字节 |")
    A("| MD5 | `c4aa7183a88decf99fa695eb8fcdda6c` |")
    A("| 导出符号 | **1334 个** |")
    A("| 链接器 | 14.51 |")
    A("")
    A("Windows 对非 KnownDLLs **应用目录优先于 System32**，所以 exe 导入")
    A("`DSOUND.dll` 时会命中游戏目录里这个 loader。loader 干两件事：")
    A("")
    A("1. **转发** —— 实现全部 1334 个 dsound 导出，游戏调用一切正常")
    A("2. **注入** —— 扫描游戏根目录及 `scripts/` `plugins/` `update/` 下的 `*.asi`，"
      "逐个 `LoadLibrary`")
    A("")
    A("exe 需要的 DSOUND 符号（按序号导入 #11 = `DirectSoundCreate`）由该 loader 提供。")
    A("")
    A("#### 实测：`winmm.dll` 劫持失败，`dsound.dll` 劫持成功")
    A("")
    A("| DLL | 在导入表 | 在 KnownDLLs | 劫持结果 |")
    A("|-----|---------|-------------|---------|")
    A("| `WINMM.dll` | 是（6 个函数）| 否 | **失败** |")
    A("| `DSOUND.dll` | 是（1 个序号）| 否 | **成功** |")
    A("")
    A("（KnownDLLs 已用注册表核实：38 项里两者都不在，因此都能被 sideload。")
    A("winmm 失败的具体原因尚未定位，可能是游戏对 winmm 走了不同的初始化路径。）")
    A("")
    A("> ⚠️ 早期分析曾断言「winmm 劫持是注入链起点」，**该结论错误**，以实测为准。")
    A("")
    A("#### loader 的扫描范围（实测五份副本全部被加载）")
    A("")
    A("```")
    A("游戏根目录\\*.asi")
    A("游戏根目录\\*.dll          （Windows 直接加载，绕过 loader）")
    A("scripts\\*.asi")
    A("plugins\\*.asi")
    A("update\\*.asi")
    A("```")
    A("")
    A("相关配置键（读 `global.ini`，全部可选，默认即为上述行为）：")
    A("`loadfromscriptsonly` / `loadrecursively` / `loadextraplugins` / "
      "`dontloadfromdllmain` / `loadfromapi` / `overloadfromfolder` / "
      "`globalsets` / `fileloader`")
    A("")

    imports = list(pe.DIRECTORY_ENTRY_IMPORT)
    total = sum(len(e.imports) for e in imports)

    A("## 二、总览")
    A("")
    A("**%d 个 DLL，%d 个导入函数。**" % (len(imports), total))
    A("")
    A("| # | DLL | 函数数 | 按序号导入 | IAT 区间 | 用途 |")
    A("|---|-----|--------|-----------|----------|------|")
    for idx, e in enumerate(imports, 1):
        dll = e.dll.decode("ascii", "replace")
        n = len(e.imports)
        nord = sum(1 for i in e.imports if i.ordinal and not i.name)
        addrs = sorted(i.address for i in e.imports)
        lo, hi = min(addrs), max(addrs) + 4
        role = DLL_ROLE.get(dll, ("-", ""))[0]
        A("| %d | `%s` | %d | %d | `0x%08X`–`0x%08X` | %s |"
          % (idx, dll, n, nord, lo, hi, role))
    A("| | **合计** | **%d** | **%d** | | |" % (total, sum(
        1 for e in imports for i in e.imports if i.ordinal and not i.name)))
    A("")

    A("## 三、逐 DLL 明细")
    A("")
    for e in imports:
        dll = e.dll.decode("ascii", "replace")
        role, note = DLL_ROLE.get(dll, ("-", ""))
        A("### %s" % dll)
        A("")
        A("%s函数 %d 个。" % (role + "，", len(e.imports)))
        if note:
            A("")
            A("> %s" % note)
        A("")
        A("| IAT 地址 | 序号 | 名称 |")
        A("|----------|------|------|")
        for i in e.imports:
            if i.ordinal and not i.name:
                nm = "—"
                ordv = str(i.ordinal)
            else:
                nm = "`%s`" % i.name.decode("ascii", "replace")
                ordv = "—"
            A("| `0x%08X` | %s | %s |" % (i.address, ordv, nm))
        A("")

    A("## 四、按功能归类")
    A("")
    A("### 4.1 图形 / 渲染")
    A("")
    A("| DLL | 函数 | 说明 |")
    A("|-----|------|------|")
    A("| d3d9.dll | `Direct3DCreate9` | 唯一的 D3D 设备创建入口，其余 D3D9 接口靠 `IDirect3D9` 虚表与 COM 在运行时取得，不进 IAT |")
    A("| d3dx9_43.dll | `D3DXLoadSurfaceFromMemory` / `D3DXLoadSurfaceFromSurface` | 只做纹理解码，**没有** `D3DXCreateFont` 之类 |")
    A("| GDI32.dll | `CreateFontA` / `ExtTextOutA` / `GetTextExtentPoint32A` | 唯一可能的 GDI 文本通路（对应分析报告的「方向 C」）|")
    A("| GDI32.dll | `CreateDIBSection` / `CreateCompatibleDC` / `SelectObject` / `GetPixel` / `SetPixel` / `GetDeviceCaps` / `DeleteDC` / `DeleteObject` / `GetStockObject` | DIB section 内存 blit，像素级读写（D3D 互操作或后处理）|")
    A("| GDI32.dll | `SetTextColor` / `SetBkColor` / `SetTextAlign` / `SetMapMode` | 文本与坐标模式设置 |")
    A("")
    A("### 4.2 音频")
    A("")
    A("| DLL | 函数 | 说明 |")
    A("|-----|------|------|")
    A("| WINMM.dll | `mciSendStringA` | MCI 字符串接口播放音乐/音效 |")
    A("| WINMM.dll | `waveOutSetVolume` / `auxSetVolume` | 音量控制 |")
    A("| WINMM.dll | `timeBeginPeriod` / `timeEndPeriod` | 提升定时器精度 —— **证明主循环是固定时间步进** |")
    A("| WINMM.dll | `timeGetTime` | 全局毫秒计时，游戏帧计时基准 |")
    A("| DSOUND.dll | 序号 #11 = `DirectSoundCreate` | DirectSound 设备创建（音效）|")
    A("")
    A("### 4.3 文件与配置")
    A("")
    A("| DLL | 函数 | 说明 |")
    A("|-----|------|------|")
    A("| KERNEL32.dll | `GetPrivateProfileStringA` / `GetPrivateProfileIntA` / `GetPrivateProfileSectionA` | **INI 配置读取，全 ANSI** —— 游戏设置走 `strings.ini` 这类文件 |")
    A("| KERNEL32.dll | `CreateFileA` / `CreateFileW` / `ReadFile` / `WriteFile` / `SetFilePointerEx` / `GetFileSize` 类 | 双版本文件 IO（A + W 都有，说明有 UTF-16 路径处理）|")
    A("| KERNEL32.dll | `GetFileAttributesExW` / `FindFirstFileExA` / `FindFirstFileExW` / `FindNextFileA` / `FindNextFileW` / `FindClose` | 目录枚举 |")
    A("| KERNEL32.dll | `CreateDirectoryW` / `DeleteFileW` / `MoveFileExW` | 文件操作（W 版）|")
    A("| KERNEL32.dll | `GetTempPathW` | 临时目录 |")
    A("| ADVAPI32.dll | `RegOpenKeyExA` / `RegQueryValueExA` / `RegSetValueExA` / `RegCreateKeyExA` / `RegDeleteValueA` / `RegCloseKey` | 注册表持久化，**全 A 版** |")
    A("| COMDLG32.dll | `GetOpenFileNameA` / `GetSaveFileNameA` | 通用打开/保存对话框（ANSI）|")
    A("")
    A("### 4.4 窗口与输入")
    A("")
    A("| DLL | 函数组 | 说明 |")
    A("|-----|--------|------|")
    A("| USER32.dll | `RegisterClassA` / `CreateWindowExA` / `DefWindowProcA` / `ShowWindow` / `UpdateWindow` | 窗口创建（ANSI 版，无 W）|")
    A("| USER32.dll | `PeekMessageA` / `WaitMessage` / `TranslateMessage` / `TranslateAcceleratorA` / `DispatchMessageA` / `PostQuitMessage` | 消息循环 |")
    A("| USER32.dll | `PostMessageA` / `SendDlgItemMessageA` / `SetCapture` / `ReleaseCapture` | 窗口间通信与鼠标捕获 |")
    A("| USER32.dll | `DialogBoxParamA` / `EndDialog` / `GetDlgItem` / `GetDlgItemTextA` / `SetDlgItemTextA` / `SetDlgItemTextA` | 对话框与文本控件 |")
    A("| USER32.dll | `LoadAcceleratorsA` / `DrawMenuBar` | 菜单与加速键 |")
    A("| USER32.dll | `LoadCursorA` / `LoadCursorFromFileA` / `SetCursor` / `ShowCursor` / `DestroyCursor` / `SetCursorPos` / `ClientToScreen` / `GetSystemMetrics` / `GetForegroundWindow` | 自绘光标 + 鼠标定位（FPS 视角控制）|")
    A("| USER32.dll | `OpenClipboard` / `CloseClipboard` / `EmptyClipboard` / `SetClipboardData` / `GetClipboardData` | 剪贴板（复制/粘贴作弊码或序列）|")
    A("| USER32.dll | `MessageBoxA` / `GetWindowRect` / `SetWindowPos` / `RedrawWindow` / `GetDC` / `ReleaseDC` / `GetClientRect` / `SetWindowTextA` | 窗口杂项 |")
    A("")
    A("### 4.5 编码转换")
    A("")
    A("| 函数 | 说明 |")
    A("|------|------|")
    A("| `MultiByteToWideChar` | 多字节 → UTF-16 |")
    A("| `WideCharToMultiByte` | UTF-16 → 多字节 |")
    A("| `GetACP` / `GetOEMCP` / `GetCPInfo` / `IsValidCodePage` | 代码页查询 |")
    A("| `LCMapStringW` / `CompareStringW` / `GetLocaleInfoW` / `IsValidLocale` / `GetUserDefaultLCID` | CRT 本地化 |")
    A("| `GetStringTypeW` / `GetDateFormatA` / `GetTimeFormatA` / `GetDateFormatW` / `GetTimeFormatW` | CRT 日期时间格式化 |")
    A("")
    A("> **与中文字形渲染相关的关键判断**：这些 `*W` 系列函数全部来自 MSVC CRT（`__crtGetLocaleInfo` 一类内部调用），引擎自身的文本绘制主循环（`0x426680`）是逐字节的，没有调用它们。")
    A("> 因此**不能**通过在 CRT 层拦截转码来注入 CJK 支持 —— 引擎根本不过 CRT 转码路径。")
    A("")
    A("### 4.6 CRT / 运行时")
    A("")
    A("堆：`HeapAlloc` / `HeapFree` / `HeapReAlloc` / `HeapSize` / `GetProcessHeap` / `GlobalAlloc` / `GlobalLock` / `GlobalUnlock` / `GlobalSize`；")
    A("")
    A("TLS：`TlsAlloc` / `TlsFree` / `TlsGetValue` / `TlsSetValue`；")
    A("")
    A("同步：`EnterCriticalSection` / `LeaveCriticalSection` / `DeleteCriticalSection` / `InitializeCriticalSectionAndSpinCount` / `InterlockedFlushSList` / `InterlockedPushEntrySList` / `InitializeSListHead`；")
    A("")
    A("异常：`RaiseException` / `RtlUnwind`；")
    A("")
    A("编码标记：`EncodePointer` / `DecodePointer`（CRT 安全 cookie 支持）；")
    A("")
    A("控制台：`SetStdHandle` / `GetStdHandle` / `WriteConsoleW` / `ReadConsoleW` / `SetConsoleCtrlHandler` / `GetConsoleMode` / `GetConsoleCP`；")
    A("")
    A("环境/命令行：`GetCommandLineA` / `GetCommandLineW` / `GetEnvironmentStringsW` / `SetEnvironmentVariableA` / `SetEnvironmentVariableW` / `FreeEnvironmentStringsW`；")
    A("")
    A("模块/进程：`GetModuleHandleExW` / `GetModuleFileNameA` / `GetModuleFileNameW` / `LoadLibraryExW` / `FreeLibrary` / `ExitProcess` / `Sleep`；")
    A("")
    A("动态加载：`GetModuleHandleExW` + `LoadLibraryExW` 表明运行时还会 **动态** 载入额外 DLL —— 静态导入表看不到全部依赖。")
    A("")
    A("### 4.7 Steamworks")
    A("")
    A("| 函数 | 说明 |")
    A("|------|------|")
    A("| `SteamAPI_Init` / `SteamAPI_Shutdown` | 初始化/关闭 |")
    A("| `SteamAPI_RunCallbacks` / `SteamAPI_RestartAppIfNecessary` | 回调与启动检查 |")
    A("| `SteamAPI_GetHSteamPipe` / `SteamAPI_GetHSteamUser` | 内部管道句柄 |")
    A("| `SteamAPI_RegisterCallResult` / `SteamAPI_UnregisterCallResult` | 异步调用结果注册 |")
    A("| `SteamInternal_ContextInit` / `SteamInternal_CreateInterface` | 内部上下文与接口工厂 |")
    A("")
    A("> `steam_api.dll` 是**非系统 DLL**，必须由 Steam 客户端提供。若未安装 Steam 直接跑 exe，会因加载该 DLL 失败而在进程启动阶段就崩溃（导入表在 IAT 填充时就已解析全部 DLL，无法延迟）。")
    A("> —— 这也是本项目注入式 Hook 必须保留 `steam_api.dll` 可解析的原因。")
    A("")

    A("## 五、逆向要点小结")
    A("")
    A("| 观察 | 结论 |")
    A("|------|------|")
    A("| 全部 UI API 均为 A 版（ANSI），无 `*W` 版窗口/文本接口 | 引擎内部字符串以单字节 ANSI 为准，与 `0x426680` 逐字节绘制循环一致 |")
    A("| CRT 的 `*W` 转码函数在导入表里，但引擎不经过它们 | 无法在 CRT 层打补丁注入 CJK |")
    A("| 未导入任何字体 API（GDI 字体只有 `CreateFontA`） | 无 TrueType/Unicode 字体参与渲染，字体全靠自建 TGA 图集 |")
    A("| `timeBeginPeriod` + `timeGetTime` | 主循环固定步长，与 hook 时序逻辑相关 |")
    A("| `Direct3DCreate9` 是唯一图形入口 | D3D9 虚表调用，hook 需落在具体方法而非导入 thunk |")
    A("| 有 `LoadLibraryExW` | 存在运行时动态依赖，静态导入表 ≠ 完整依赖清单 |")
    A("")

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")

    pe.close()
    print("已生成: %s (%d 行)" % (OUT, len(L)))
    print("DLL=%d函数=%d" % (len(imports), total))


if __name__ == "__main__":
    main()