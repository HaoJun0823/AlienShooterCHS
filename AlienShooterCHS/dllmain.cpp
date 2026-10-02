// dllmain.cpp —— DLL 入口
//
// 注入由 ASI loader 负责，本 DLL 只在进程内安装 hook。
//
// 采用 freestanding 链接（/NODEFAULTLIB + 自定义入口 CHS_DllEntry），
// 不依赖 CRT —— DllMain 阶段 CRT 尚未就绪，且 /MT 的入口符号修饰
// 会与自定义入口冲突。这样也避免向游戏进程注入额外 CRT 运行时。

#include "pch.h"
#include "block_layout.h"

extern "C" __declspec(dllexport) void __cdecl CHS_Init();
extern "C" __declspec(dllexport) void __cdecl CHS_SetExeBase(HMODULE h);
extern "C" __declspec(dllexport) void __cdecl CHS_Dump();
extern "C" __declspec(dllexport) void __cdecl CHS_SetTextDump(int on);

// 打开日志（不安装 hook）。必须在任何其它逻辑之前调用 ——
// 这样即便后续 hook 安装失败，也能确认"DLL 确实被加载过"。
extern "C" void __cdecl CHS_LogBoot(HMODULE hExe);

// 崩溃取证：装 VEH，任何异常先落盘 EIP 再交给原处理链。
// 必须在装 hook 之前就位 —— 否则 hook 一崩就什么都看不到。
extern "C" void __cdecl InstallCrashHandler();

// SEH 支撑：解析 msvcrt.dll 的 _except_handler3。
// ★ 必须早于任何 __try 执行 —— 否则跳转桩跳到 0 地址。
extern "C" void __cdecl CHS_InitSeh();

// 真正的入口。参数约定与 DllMain 相同。
extern "C" __declspec(dllexport) BOOL __stdcall CHS_DllEntry(HMODULE hModule,
                                                              DWORD reason,
                                                              LPVOID reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // 宿主 exe 基址（GetModuleHandle(NULL) 返回主可执行模块）
        CHS_SetExeBase(GetModuleHandleA(NULL));
        // 第一件事就落盘日志：这是"被加载"的最直接证据
        CHS_LogBoot(GetModuleHandleA(NULL));
        InstallCrashHandler();
        CHS_InitSeh();
        // 再安装 hook。目标函数是纯绘制路径，不依赖 steam_api 初始化。
        CHS_Init();
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    // 保留标准 DllMain 符号（万一有工具链查它）；实际入口是 CHS_DllEntry。
    return CHS_DllEntry(hModule, reason, lpReserved);
}
