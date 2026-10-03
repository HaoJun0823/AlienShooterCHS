// hook.cpp —— Alien Shooter 中文字形渲染 Hook
//
// 目标函数：sub_426680 @ RVA 0x00026680（文本绘制主循环）
//
// 【本阶段：探测期】只观测、不改行为。
//   流程：目标入口 E9 → CHS_Hook（裸汇编，栈纪律完全可控）
//           → push block; call CHS_ProbeBlock（记录 Block 状态）
//           → push block; call CjkDispatch（含中文则自己画并跳过原流程）
//           → 未接管：复现原函数前 5 字节指令 → jmp 回 target+5
//           → 原函数剩余部分照常执行，epilogue 直接返回原调用方
//
// 不使用 CRT（fopen/printf）：DllMain 阶段调用 CRT 有风险，
// 日志改用 Win32 文件 API。
//
// 日志落盘（2026-10-02 补）：主日志 <exe目录>\chs_probe.log，
// 兜底 exe 同名 + DLL 所在目录 chs_probe_DLLDIR.log + %TEMP%\chs_probe_TEMP.log。
// 之前只写 exe 目录一处，若 DLL 未被加载或目录不可写就毫无痕迹。
//
// 编译：Release|Win32。注入由 ASI loader 负责 —— 游戏目录下的 dsound.dll
// 实为 Ultimate ASI Loader 9.7.4，它扫描 update\*.asi。
// ★ winmm.dll 劫持实测不生效（虽然它在导入表里且不在 KnownDLLs），别再用。

#include "pch.h"
#include "block_layout.h"

// ================================================================ 全局

HMODULE       g_exe = NULL;        // 宿主 exe 基址（ASLR 下用于 RVA 换算）
// g_log 定义在下方「日志」小节，与落盘逻辑放在一起

// ================================================================ 无 CRT 依赖的小工具
// 本 DLL 不链接 CRT（避免 DllMain 阶段 CRT 未就绪 + 入口符号修饰问题）。
//
// 关键点：MSVC 把 memcpy/memset 当内建函数，即使源码不写也会生成对
// `_memcpy`/`_memset` 的外部引用。freestanding 链接（/NODEFAULTLIB）下这些
// 符号无处可寻 —— 静态 CRT 库里没有它们（它们在 msvcrt.dll 里）。
// 解法：#pragma function 把它们降级为普通函数，再提供自己的实现。

#pragma function(memcpy, memset)

extern "C" void* __cdecl memcpy(void* dst, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

extern "C" void* __cdecl memset(void* dst, int c, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

// 栈探测存根：x86 MSVC 在函数栈帧 >4KB 时会调用 __chkstk。
// 本 DLL 栈帧都远小于 4KB（缓冲区已缩到 1KB），所以这里只需原样返回。
extern "C" void __cdecl __chkstk(void) {}

// 浮点存根：链接器需要它来定位浮点常量。
extern "C" int _fltused = 1;

static size_t my_strlen(const char* s)
{
    const char* p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

static char* my_strrchr(const char* s, char c)
{
    for (char* p = (char*)s + my_strlen(s); p >= s; --p)
        if (*p == c) return p;
    return NULL;
}

static void my_strcpy(char* dst, size_t cap, const char* src)
{
    size_t i = 0;
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static void my_strcat(char* dst, size_t cap, const char* src)
{
    size_t d = my_strlen(dst);
    size_t s = my_strlen(src);
    if (d + 1 >= cap) return;
    size_t n = (d + s + 1 <= cap) ? s : (cap - d - 1);
    for (size_t i = 0; i < n; ++i) dst[d + i] = src[i];
    dst[d + n] = 0;
}

static int my_strncmp(const char* a, const char* b, int n)
{
    for (int i = 0; i < n; ++i) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x != y) return (int)x - (int)y;
        if (!x) break;
    }
    return 0;
}

// ================================================================ 日志（Win32 API）
//
// ★ 落盘位置：<exe 目录>\update\chs_probe.log   —— 单一文件，不再散落。
//
//   踩过的坑：曾为了排查"到底哪个副本被加载"把日志镜像到 exe 目录 /
//   DLL 目录 / %TEMP% 三处，结果游戏目录、scripts\、plugins\、TEMP
//   一共落下 6 个 log 文件，极其碍事。既然注入方式已确定（dsound.dll
//   劫持，loaderdll 扫根目录 *.asi），就不需要再靠多路径区分身份了。
//
//   选 update\ 的原因：本项目所有产物（asi、日志）集中在一处，
//   清理时删一个目录即可，不会污染游戏根目录。

static HANDLE g_log = INVALID_HANDLE_VALUE;
static char   g_logPath[MAX_PATH] = {0};

// ---- 前向声明 ----
// 定义分布在下方各小节；这里提前声明是因为日志/崩溃取证要在它们之前可用。
static unsigned long PEImageSize(HMODULE h);
static int  my_snprintf(char* buf, size_t cap, const char* fmt, ...);
static void CrashLog(const char* s);
static LONG __stdcall CrashHandler(EXCEPTION_POINTERS* ep);
extern "C" void __cdecl InstallCrashHandler();

// 崩溃取证用全局
static BYTE*         g_selfBase    = NULL;
static unsigned long g_selfSize    = 0;
static volatile long g_inProbe     = 0;      // 非 0 = 崩在 ProbeBlock 里
static volatile long g_crashLogged = 0;      // 只记第一次，避免异常风暴刷屏

// 绘制调用计数（崩溃日志要用它看"崩在第几次调用"；正文见「探测统计」小节）
static unsigned long g_callCount = 0;

static void LogWrite(const char* s)
{
    if (g_log == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(g_log, s, (DWORD)my_strlen(s), &wr, NULL);
}

static void LogOpen()
{
    if (g_log != INVALID_HANDLE_VALUE) return;

    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(g_exe, exePath, MAX_PATH) == 0) return;

    // <exe 目录>\update\chs_probe.log
    for (int i = 0; i < MAX_PATH && exePath[i]; ++i) g_logPath[i] = exePath[i];
    g_logPath[MAX_PATH - 1] = 0;
    char* slash = my_strrchr(g_logPath, '\\');
    if (slash) slash[1] = 0;
    my_strcat(g_logPath, MAX_PATH, "update\\chs_probe.log");

    g_log = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_log == INVALID_HANDLE_VALUE) {
        // 主日志建不起来（目录不存在/无权限）。退而求其次写 exe 目录根部，
        // 至少别让这一次运行的证据彻底丢失。
        my_strcat(g_logPath, MAX_PATH, "..\\chs_probe.log");
        g_log = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    // ★ 用 GetModuleHandleEx(FROM_ADDRESS) 取本 DLL 的真实句柄。
    //   之前直接把函数地址 `&LogOpen` 当 HMODULE 传给 GetModuleFileNameA，
    //   结果失败 → 日志里 "loaded as = (unknown)"，无法确认跑的是哪个副本。
    //   ASI loader 会把 DLL 改名成 .asi 加载，靠文件名找不可靠，
    //   按"地址落在哪个模块里"查才是稳的。
    HMODULE hSelf = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCSTR)&LogOpen, &hSelf) && hSelf) {
        g_selfBase = (BYTE*)hSelf;
        g_selfSize = PEImageSize(hSelf);
    }

    char selfPath[MAX_PATH] = {0};
    if (hSelf) GetModuleFileNameA(hSelf, selfPath, MAX_PATH);

    char buf[640];
    int len = wsprintfA(buf,
        "=== AlienShooterCHS probe ===\r\n"
        "pid       = %lu\r\n"
        "loaded as = %s\r\n"
        "self path = %s\r\n"
        "exe base  = %08lX\r\n"
        "target    = %08lX (RVA 0x%06X)\r\n"
        "log path  = %s\r\n\r\n",
        (unsigned long)GetCurrentProcessId(),
        selfPath[0] ? selfPath : "(unknown)",
        selfPath[0] ? selfPath : "(unknown)",
        (unsigned long)g_exe,
        (unsigned long)((BYTE*)g_exe + RVA_DrawText),
        RVA_DrawText,
        g_logPath);

    if (g_log != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(g_log, buf, len, &wr, NULL);
    }
}

// ================================================================ SEH 支撑（freestanding）
// 本项目不链接 CRT（/NODEFAULTLIB + 自定义入口），而 MSVC 的 __try/__except
// 需要两个 CRT 提供的符号：
//   __except_handler3  —— x86 SEH 帧的分派器（trylevel/scope table 展开）
//   __load_config_used —— 映像的 IMAGE_LOAD_CONFIG_DIRECTORY32
// 前者直接从 msvcrt.dll 导入（它一定已加载，见 build.bat 里 msvcrt.lib 的注释）；
// 后者自己给一个最小版本：Size=0x40，其余全 0 → SEHandlerTable/Count = 0，
// 等价于 /SAFESEH:NO（我们的 DLL 本来就没有 @feat.00 SafeSEH 表）。
//
// C 源码里写 `_except_handler3` / `_load_config_used`，x86 __cdecl 会自动加
// 一个下划线前缀，正好是链接器要的 `__except_handler3` / `__load_config_used`。
//　实测：VC 2017 / SDK 10 的 msvcrt.lib、libcmt.lib、libvcruntime.lib 里
//　**都没有** __except_handler3 这个符号，所以 __declspec(dllimport) 解析不了。
//　但 msvcrt.dll 确实导出了 `_except_handler3`，而且 MSVC 为 x86 __try 生成的
//　正是 handler3 版本的作用域表 —— ABI 完全对得上。
//　那就自己写一个等价于导入 thunk 的裸跳转（/MD 链接出来的机器码一模一样）：
//      jmp dword ptr [__imp__except_handler3]
static void* g_pExceptHandler3 = NULL;

extern "C" __declspec(naked) int __cdecl _except_handler3(void*, void*, void*, void*)
{
    __asm { jmp dword ptr [g_pExceptHandler3] }
}

extern "C" void __cdecl CHS_InitSeh()
{
    HMODULE m = GetModuleHandleA("msvcrt.dll");
    if (m) g_pExceptHandler3 = (void*)GetProcAddress(m, "_except_handler3");
    if (!g_pExceptHandler3 && !m) m = LoadLibraryA("msvcrt.dll");
    if (!g_pExceptHandler3 && m) g_pExceptHandler3 = (void*)GetProcAddress(m, "_except_handler3");
}

#pragma section(".rdata$lcfg", read)
extern "C" __declspec(allocate(".rdata$lcfg"))
const unsigned long _load_config_used[16] = { 0x40, 0, 0, 0, 0, 0, 0, 0,
                                              0,    0, 0, 0, 0, 0, 0, 0 };

// ================================================================ 崩溃取证
// 探测 DLL 跑在游戏主循环里，一旦它自己出事，游戏就崩，而我们拿不到任何
// 线索（日志停在 hook armed，看不出崩在哪）。
// 装一个 Vectored Exception Handler：任何异常先落盘再交给原来的处理链，
// 这样下次再崩就能直接看到 EIP 落在谁的代码里。

// 读 PE 头拿 SizeOfImage，用于判断 EIP 是否落在本 DLL 内
static unsigned long PEImageSize(HMODULE h)
{
    if (!h) return 0;
    const BYTE* b = (const BYTE*)h;
    if (b[0] != 'M' || b[1] != 'Z') return 0;
    unsigned long lf = *(unsigned long*)(b + 0x3C);
    const BYTE* nt = b + lf;
    if (nt[0] != 'P' || nt[1] != 'E') return 0;
    // OptionalHeader.SizeOfImage 在 NT 头 +0x18（PE32）
    return *(unsigned long*)(nt + 0x18 + 0x38);
}

static void CrashLog(const char* s)
{
    if (g_log == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(g_log, s, (DWORD)my_strlen(s), &wr, NULL);
    FlushFileBuffers(g_log);
}

static LONG __stdcall CrashHandler(EXCEPTION_POINTERS* ep)
{
    if (InterlockedExchange(&g_crashLogged, 1)) return EXCEPTION_CONTINUE_SEARCH;

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    BYTE* eip  = (BYTE*)ep->ExceptionRecord->ExceptionAddress;
    DWORD eax  = ep->ContextRecord->Eax;
    DWORD ecx  = ep->ContextRecord->Ecx;
    DWORD edx  = ep->ContextRecord->Edx;
    DWORD ebx  = ep->ContextRecord->Ebx;
    DWORD esi  = ep->ContextRecord->Esi;
    DWORD edi  = ep->ContextRecord->Edi;
    DWORD ebp  = ep->ContextRecord->Ebp;
    DWORD esp  = ep->ContextRecord->Esp;

    const char* where = "outside known modules";
    unsigned long off = 0;
    if (g_selfBase && g_selfSize &&
        (BYTE*)eip >= g_selfBase && (BYTE*)eip < g_selfBase + g_selfSize) {
        where = "INSIDE AlienShooterCHS";
        off   = (unsigned long)(eip - g_selfBase);
    } else if (g_exe && (BYTE*)eip >= (BYTE*)g_exe &&
               (BYTE*)eip < (BYTE*)g_exe + 0x400000) {
        where = "inside AlienShooter.exe";
        off   = (unsigned long)(eip - (BYTE*)g_exe);
    }

    // EIP 落在哪个模块 —— 第四轮崩在 768FCA5E，日志只说"outside known
    // modules"，还得靠猜。直接问系统要模块名。
    char mod[MAX_PATH];
    mod[0] = 0;
    {
        typedef int (__stdcall *GMHE_t)(unsigned long, const char*, HMODULE*);
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        GMHE_t gmhe = k32 ? (GMHE_t)GetProcAddress(k32, "GetModuleHandleExA") : NULL;
        HMODULE hm = NULL;
        if (gmhe && gmhe(0x00000004u /*FROM_ADDRESS*/ | 0x00000002u /*UNCHANGED_REFCOUNT*/,
                         (const char*)eip, &hm))
            GetModuleFileNameA(hm, mod, MAX_PATH);
        if (!mod[0]) my_strcpy(mod, sizeof(mod), "(unknown)");
    }
    const char* modBaseName = mod;
    for (const char* q = mod; *q; ++q) if (*q == '\\') modBaseName = q + 1;

    void* fault = NULL;
    const char* rw = "";
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        fault = (void*)ep->ExceptionRecord->ExceptionInformation[1];
        rw = ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ";
    }

    char b[768];
    my_snprintf(b, sizeof(b),
        "\r\n=== CRASH ===\r\n"
        "code     = 0x%08lX\r\n"
        "EIP      = %p  (%s, +0x%lX)\r\n"
        "module   = %s\r\n"
        "fault    = %p %s\r\n"
        "inProbe  = %ld\r\n"
        "EAX=%08lX ECX=%08lX EDX=%08lX EBX=%08lX\r\n"
        "ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX\r\n"
        "calls    = %lu\r\n"
        "=== END CRASH ===\r\n",
        (unsigned long)code, (void*)eip, where, (unsigned long)off,
        modBaseName, fault, rw,
        (long)g_inProbe,
        (unsigned long)eax, (unsigned long)ecx, (unsigned long)edx, (unsigned long)ebx,
        (unsigned long)esi, (unsigned long)edi, (unsigned long)ebp, (unsigned long)esp,
        g_callCount);
    CrashLog(b);
    return EXCEPTION_CONTINUE_SEARCH;
}

extern "C" void __cdecl InstallCrashHandler()
{
    typedef PVOID (__stdcall *AddVEH_t)(ULONG, PVOID);
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (k32) {
        AddVEH_t add = (AddVEH_t)GetProcAddress(k32, "AddVectoredExceptionHandler");
        if (add) { add(1, (PVOID)CrashHandler); return; }
    }
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)CrashHandler);
}

// 简易格式化：支持 %d %u %x %X %p %c %s %f %%
// 不依赖 CRT 的 *_s 变体（它们只在 UCRT inline 头里可见，链接方式易踩坑）。
// 返回写入的字符数。
//
// ★ 支持宽度/零填充（%02X / %06X / %8.2f）。探针日志大量用 %02X 打字节，
//   早期版本没实现宽度，结果整行退化成字面量 "%02X %02X ..."，
//   完全看不出实际字节值，排障时非常误导。补上 flags/width 解析。
static void emit_num_pad(char*& p, char* end, unsigned long uv,
                         int width, int zeros, int neg)
{
    char t[24];
    int k = 0;
    do { t[k++] = (char)('0' + uv % 10); uv /= 10; } while (uv);
    int digits = k;
    if (neg) digits++;
    int pad = width - digits;
    while (pad > 0 && p + 1 < end) {
        *p++ = zeros ? '0' : ' ';
        pad--;
    }
    if (neg && p + 1 < end) *p++ = '-';
    while (k && p + 1 < end) *p++ = t[--k];
}

static int my_vsnprintf(char* buf, size_t cap, const char* fmt, va_list ap)
{
    char* p = buf;
    char* end = buf + cap;
    if (cap == 0) return 0;

    for (const char* q = fmt; *q; ++q) {
        if (*q != '%') { if (p + 1 < end) *p++ = *q; continue; }
        ++q;

        // ---- 解析 flags / width / 精度 ----
        int zeros = 0, width = 0;
        while (*q == '0' || *q == '-' || *q == '+' || *q == ' ') {
            if (*q == '0') zeros = 1;
            ++q;
        }
        while (*q >= '0' && *q <= '9') {
            width = width * 10 + (*q - '0');
            ++q;
        }
        int prec = -1;
        if (*q == '.') {
            ++q;
            prec = 0;
            while (*q >= '0' && *q <= '9') {
                prec = prec * 10 + (*q - '0');
                ++q;
            }
        }
        // ---- 长度修饰符 l / ll ----
        // Win32 下 long 与 long long 同为 32 位（LLP64），取值方式一致，
        // 直接跳过 'l' 即可（原有代码漏了它，导致 "%08lX" 整串退化成字面量）。
        while (*q == 'l' || *q == 'h' || *q == 'z' || *q == 'j' || *q == 't') ++q;

        switch (*q) {
        case 'd': {
            long v = va_arg(ap, long);
            unsigned long uv = (v < 0) ? (unsigned long)(-v) : (unsigned long)v;
            emit_num_pad(p, end, uv, width, zeros, v < 0);
            break;
        }
        case 'u': {
            unsigned long v = va_arg(ap, unsigned long);
            emit_num_pad(p, end, v, width, zeros, 0);
            break;
        }
        case 'x': case 'X': {
            unsigned long v = va_arg(ap, unsigned long);
            const char* digits = (*q == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            char t[24]; int k = 0;
            do { t[k++] = digits[v & 0xF]; v >>= 4; } while (v);
            int pad = width - k;
            while (pad > 0 && p + 1 < end) { *p++ = zeros ? '0' : ' '; pad--; }
            while (k && p + 1 < end) *p++ = t[--k];
            break;
        }
        case 'p': {
            void* v = va_arg(ap, void*);
            unsigned long uv = (unsigned long)(ULONG_PTR)v;
            char t[24]; int k = 0;
            do { t[k++] = "0123456789ABCDEF"[uv & 0xF]; uv >>= 4; } while (uv);
            while (k && p + 1 < end) *p++ = t[--k];
            break;
        }
        case 'c': { char v = (char)va_arg(ap, int); if (p + 1 < end) *p++ = v; break; }
        case 's': {
            const char* v = va_arg(ap, const char*);
            if (!v) v = "(null)";
            while (*v && p + 1 < end) *p++ = *v++;
            break;
        }
        case 'f': {
            double v = va_arg(ap, double);
            if (prec < 0) prec = 2;
            unsigned long scale = 1;
            for (int i = 0; i < prec; ++i) scale *= 10;
            long scaled = (long)(v * (double)scale + (v < 0 ? -0.5 : 0.5));
            unsigned long av = (scaled < 0) ? (unsigned long)(-scaled) : (unsigned long)scaled;
            emit_num_pad(p, end, av / scale, width, zeros, scaled < 0);
            if (p + 1 < end) *p++ = '.';
            unsigned long fr = av % scale;
            for (unsigned long div = scale / 10; div; div /= 10) {
                if (p + 1 < end) *p++ = (char)('0' + (fr / div) % 10);
            }
            break;
        }
        case '%': if (p + 1 < end) *p++ = '%'; break;
        default:  if (p + 1 < end) *p++ = '%';
                  if (*q && p + 1 < end) *p++ = *q; break;
        }
    }
    *p = 0;
    return (int)(p - buf);
}

static int my_snprintf(char* buf, size_t cap, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = my_vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

// 简易格式化：转发到 my_snprintf，保留宽度/精度（%02X / %06X / %.2f 等）
static void LogPrintf(const char* fmt, ...)
{
    if (g_log == INVALID_HANDLE_VALUE) return;
    char out[1024];

    va_list ap;
    va_start(ap, fmt);
    // 直接调 my_vsnprintf，保留 width/precision（%02X / %06X / %.2f 等）
    int n = my_vsnprintf(out, sizeof(out), fmt, ap);
    va_end(ap);

    if (n > 0) {
        DWORD wr = 0;
        WriteFile(g_log, out, (DWORD)((n < (int)sizeof(out)) ? n : (int)sizeof(out) - 1), &wr, NULL);
    }
}

// ================================================================ Block 访问器
// ★ 全部偏移已用 IDA 反汇编逐条核对（sub_426680, 2026-10-02）
//
//   4266A8  mov esi, ecx              ← this = Block（__thiscall）
//   4266AA  mov ebx, [esi+1Ch]         ← Block+0x1C = font
//   4266C1  cmp dword ptr [ebx+70h], 7Eh
//   426740  mov eax, [esi+74h]         ← Block+0x74 = char*（不是 char**！）
//   426748  mov dl, [eax]
//
// ★★ 之前把 +0x74 声明成 char** 并再解一次（`*BlockText(block)`），
//   多解一层指针 → 读到野地址 → 第一次实机就崩。
//   正确类型是 char*，直接用。

static inline void*   BlockFont(void* b)       { return *(void**)((char*)b + 0x1C); }
static inline float   BlockPosX(void* b)       { return *(float*)((char*)b + 0x30); }
static inline float   BlockPosY(void* b)       { return *(float*)((char*)b + 0x34); }
static inline char*   BlockText(void* b)       { return *(char**)((char*)b + 0x74); }
static inline int     BlockLength(void* b)     { return *(int*)((char*)b + 0x7C); }
static inline int     BlockAlignFlags(void* b) { return *(int*)((char*)b + 0x80); }
static inline int     BlockLinesV(void* b)     { return *(int*)((char*)b + 0x84); }
static inline int     BlockLinesH(void* b)     { return *(int*)((char*)b + 0x88); }

static inline int     FontType(void* f)        { return *(int*)((char*)f + 0x70); }
static inline float   GlyphCharWidth(void* f)  { return *(float*)((char*)f + 0x1C); }
static inline float   GlyphLineHeight(void* f) { return *(float*)((char*)f + 0x20); }

// ================================================================ 探测统计

static unsigned long g_byteHist[256]  = {0};
static unsigned long g_ctrlCount      = 0;
static unsigned long g_escCount       = 0;
static unsigned long g_fontTagCount   = 0;
static unsigned long g_newlineCount   = 0;
static unsigned long g_tabCount       = 0;
static unsigned long g_gdiPathCount   = 0;
static unsigned long g_badBlockCount  = 0;   // 指针不可读被拦下的次数
static unsigned long g_alignHist[8]   = {0};
static unsigned long g_fontTypeHist[8]= {0};
static unsigned long g_charWidthMin   = 0xFFFFFFFFul;
static unsigned long g_charWidthMax   = 0;
static int           g_dumpText       = 0;
static unsigned long g_lastDumpCall   = 0;

static void AutoDumpIfDue();   // 定义见文件末尾（CHS_Dump 附近）

// ================================================================ 探测主体
// 纯观测，绝不修改任何游戏数据。
//
// ★★ 这是第一次实机崩溃的教训：探测代码本身绝不能成为崩溃源。
//   任何指针解引用前先验证可读（IsBadReadPtr 已导入并被 loader 提供，
//   但更稳的是限制在已知有效范围内 + 上界检查）。
//   崩溃 → hook 白装，且拿不到任何数据。

// 安全读取一个 int：地址可疑时返回 def 而不是崩
static int SafeReadInt(const void* p, int def)
{
    if (!p) return def;
    unsigned char b[4];
    if (IsBadReadPtr(p, 4)) return def;
    for (int i = 0; i < 4; ++i) b[i] = ((const unsigned char*)p)[i];
    return *(int*)b;
}

// 安全读取一个指针
static void* SafeReadPtr(const void* p, void* def)
{
    if (!p) return def;
    if (IsBadReadPtr(p, 4)) return def;
    return *(void* const*)p;
}

// 安全读取一个 float：按位搬运，不做真正的浮点加载。
// 读失败返回 def。早期版本直接解引用 float*，字段不可读时就崩在探针里。
static float SafeReadFloat(const void* p, float def)
{
    if (!p || IsBadReadPtr(p, 4)) return def;
    int bits = 0;
    const unsigned char* b = (const unsigned char*)p;
    bits = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
    float f;
    char* dst = (char*)&f;
    dst[0] = (char)(bits      );
    dst[1] = (char)(bits >>  8);
    dst[2] = (char)(bits >> 16);
    dst[3] = (char)(bits >> 24);
    return f;
}

// 不碰 FPU 的 float→long。
// 老游戏（本作 2008 年引擎）普遍重度依赖 x87 状态，探针里用 `fld/fistp`
// 做浮点转换有污染 FPU 寄存器栈的风险；直接按 IEEE754 位模式手算更安全。
static long BitsToLong(unsigned long bits)
{
    int          sign = (bits >> 31) ? -1 : 1;
    int          exp  = (int)((bits >> 23) & 0xFF);
    unsigned long man = bits & 0x7FFFFF;
    if (exp == 0) return 0;                       // 0 或非规格化
    exp -= 127;
    if (exp < 0)  return 0;                       // |v| < 1
    if (exp > 30) return sign * 0x3FFFFFFFl;      // 溢出钳位
    return sign * (long)((man | 0x800000) >> (23 - exp));
}

static long FontCharWidth(void* f)  { return BitsToLong((unsigned long)SafeReadInt((char*)f + 0x1C, 0)); }
static long FontLineHeight(void* f) { return BitsToLong((unsigned long)SafeReadInt((char*)f + 0x20, 0)); }

static void ProbeBlockInner(void* block)
{
    // ★ 最外层守卫：block 本身不可读就直接返回，绝不碰任何字段
    if (!block || IsBadReadPtr(block, 0x90)) { g_badBlockCount++; return; }

    g_callCount++;

    // 前 8 次调用无条件留痕：若一次都没有，说明崩在进入 ProbeBlock 之前
    // （例如 this 指针被破坏），而不是崩在探测逻辑里。
    if (g_callCount <= 8) {
        LogPrintf("[probe #%lu] block=%p\r\n", g_callCount, block);
        FlushFileBuffers(g_log);
    }

    // ★ +0x74 是 char*，不是 char**。之前多解一层 → 野指针 → 崩溃。
    void* font = SafeReadPtr((char*)block + 0x1C, NULL);
    char* text = (char*)SafeReadPtr((char*)block + 0x74, NULL);
    int   len  = SafeReadInt((char*)block + 0x7C, 0);
    int   af   = SafeReadInt((char*)block + 0x80, 0);

    // ---- font 对象 ----
    // ★ RVA_NullGlyph 是 RVA，不是绝对地址。exe 基址在 ASLR 下每次运行都变
    //   （实测 0x400000 → 0x3E0000），必须 g_exe + RVA 才是运行时地址。
    void* nullGlyph = g_exe ? (void*)((BYTE*)g_exe + RVA_NullGlyph) : NULL;
    if (!font || (nullGlyph && font == nullGlyph)) { g_gdiPathCount++; return; }
    if (IsBadReadPtr(font, 0x24)) { g_badBlockCount++; return; }

    int ftype = SafeReadInt((char*)font + 0x70, 0);
    g_fontTypeHist[(ftype < 0 ? 0 : (ftype < 8 ? ftype : 7))]++;
    if (ftype <= 126) { g_gdiPathCount++; return; }   // <= 126 走 GDI，见 0x4266C1

    unsigned long w = (unsigned long)FontCharWidth(font);
    if (w < g_charWidthMin) g_charWidthMin = w;
    if (w > g_charWidthMax) g_charWidthMax = w;

    g_alignHist[(af & 0x70) >> 4]++;

    // ---- 字节分布扫描 ----
    if (text && !IsBadReadPtr(text, 1)) {
        // 长度上限：不信任 Block+0x7C，扫描到 NUL 或 4096 为止
        int limit = 4096;
        if (len > 0 && len < limit) limit = len;

        int scanned = 0;
        for (int i = 0; i < limit; ++i) {
            if (IsBadReadPtr(text + i, 1)) break;
            unsigned char c = (unsigned char)text[i];
            ++scanned;
            if (c == 0) break;
            if (c == 0x1B) { g_escCount++; ++i; continue; }   // ESC + 颜色码
            if (c == 0x0A) { g_newlineCount++; continue; }
            if (c == 0x0D) { continue; }
            if (c == 0x09) { g_tabCount++;  continue; }
            if (c < 0x20)   { g_ctrlCount++; continue; }
            g_byteHist[c]++;
        }
        if (scanned >= 6 && !IsBadReadPtr(text, 6)
            && my_strncmp(text, "<Font=", 6) == 0) g_fontTagCount++;
    }

    // ---- 逐次 dump（默认关；需要时用 CHS_SetTextDump(1) 开）----
    if (g_dumpText && text && (g_callCount - g_lastDumpCall) < 200
        && !IsBadReadPtr(text, 1)) {
        g_lastDumpCall = g_callCount;

        char head[320];
        my_snprintf(head, sizeof(head),
            "[%lu] len=%d align=0x%X linesH=%d linesV=%d w=%ld h=%ld xBits=%08lX yBits=%08lX text=\"",
            g_callCount, len, af,
            SafeReadInt((char*)block + 0x88, 0),
            SafeReadInt((char*)block + 0x84, 0),
            FontCharWidth(font), FontLineHeight(font),
            (unsigned long)SafeReadInt((char*)block + 0x30, 0),
            (unsigned long)SafeReadInt((char*)block + 0x34, 0));
        LogPrintf("%s", head);

        // 逐字节直写日志，避免构造大栈缓冲（编译器会为其生成 memset/memcpy 内建引用）
        int n = (len > 0 && len < 1000) ? len : 1000;
        for (int i = 0; i < n; ++i) {
            if (IsBadReadPtr(text + i, 1)) break;
            unsigned char c = (unsigned char)text[i];
            if (!c) break;
            if (c >= 0x20 && c < 0x7F) {
                char one[2];
                one[0] = (char)c;
                one[1] = 0;
                LogPrintf("%s", one);
            } else {
                char one[8];
                my_snprintf(one, sizeof(one), "\\x%X", (int)c);
                LogPrintf("%s", one);
            }
        }
        LogPrintf("\"\n");
    }

    AutoDumpIfDue();
}

// 裸汇编 hook 的调用目标。
// 外面套一层只为了维护 g_inProbe：崩溃时 VEH 打印它，
// 非 0 就说明异常发生在探测代码内部，0 则是别处（trampoline/原函数）出的事。
// 这样"崩在哪"不需要猜。
extern "C" void __cdecl ProbeBlock(void* block)
{
    InterlockedIncrement(&g_inProbe);
    ProbeBlockInner(block);
    InterlockedDecrement(&g_inProbe);
}

// ================================================================ 方向 B：自建 D3DX 字体接管 CJK
//
// ★★ 方向 A（复用引擎自带 sub_4306A0）已实机证伪，完整结论记在此，别再走一遍：
//
//   sub_4306A0 根本不是 GDI/TTF 通路，而是引擎的 **D3D9 位图字形四边形渲染器**：
//     4306A6  cmp [esi+0E34h], 0        ; esi = screen (dword_502AD4)
//     4306D3  mov ecx, [esi+0E34h]      ; this = 字形渲染器对象
//     4306F4  call sub_41B710
//   sub_41B710 逐字符从 **[this+0x74 + 16*ch]** 取字形度量（left/top/right/bottom
//   四个 float，一条 16 字节），手工填顶点缓冲后 DrawPrimitive 画三角形：
//     41B74E  SetFVF(0x144)             ; XYZRHW|DIFFUSE|TEX1，步长 0x1C = 28 字节
//     41B7C7  Lock(0,0,&p,DISCARD)      ; 顶点缓冲
//     41BB0B  DrawPrimitive(4, 0, ebx)  ; D3DPT_TRIANGLELIST
//     → 度量表长度 = (0x1074-0x74)/16 = **256 条**，本身就是位图字体，装不下 CJK。
//
//   而且这条通路在这份 exe 里是**从未初始化过的死代码**：
//     41B719  cmp [esi+5Ch], 0          ; D3D device —— 非空，通过了
//     41B72B  mov eax, [esi+1074h]      ; IDirect3DStateBlock9*
//     41B734  call [ecx+10h]            ; → Capture()   ★ 实机崩在这里
//   崩溃取证：EIP=41B734，EAX=07E2831C，ECX=00000000（[EAX] 即虚表为空）
//     → 状态块从未 CreateStateBlock，整条路根本没启用过。
//
//   附带纠错：sub_4306A0 的真实签名是
//     sub_4306A0(screen, float x, float y, int color, const char* text)
//   而不是 (x, y, text, n)。方向 A 把 -1 当 text 传了进去（幸好先崩在状态块）。
//
// 方向 B：绕过引擎的字形表，**自己用 D3DX 字体画**。
//   · 设备指针：[[screen+0xE34] + 0x5C]（引擎自己就在上面 SetFVF/SetStreamSource）
//   · 字体：d3dx9_43.dll 的 D3DXCreateFontA（游戏已加载该 DLL，导出存在）
//     CharSet = GB2312_CHARSET(134)，走系统宋体 → GBK 字节串可直接画
//   · 绘制：ID3DXFont::DrawTextA（虚表 0x20），pSprite 传 NULL 让 D3DX 自己管
//   · 坐标：sub_41B710 里 x/y 原样写进顶点（XYZRHW = 屏幕像素），只有字形宽高
//     才乘 [TR+0x68 或 +0x6C] / [TR+0x70]。所以 Block 的 posX/posY 就是屏幕
//     像素，不用换算；只有**字号**要乘这个缩放系数。
//   · 任何一步失败都返回 0 回退原位图通路（最坏情况：中文不显示，但不崩）
//
// 行为约束：
//   · 纯 ASCII 文本：完全不动，继续走位图字体（零改动、零风险）
//   · 含 >=0x80 的文本：整块交给 D3DX 字体，**跳过原位图绘制**
//   · <update>\chs_off.txt 存在 → 整个接管关闭（不重建 DLL 的逃生开关）

static unsigned long g_cjkCalls      = 0;   // 侦测到含 >=0x80 的块
static unsigned long g_cjkDrawn      = 0;   // 真正用 D3DX 字体画了
static unsigned long g_cjkNoDev      = 0;   // 拿不到 D3D 设备，回退
static unsigned long g_cjkNoFont     = 0;   // D3DX 字体创建失败，回退
static unsigned long g_cjkConvFail   = 0;   // 编码转换失败，回退
static unsigned long g_cjkBadPtr     = 0;   // 指针可疑，回退
static unsigned long g_cjkBadVtbl    = 0;   // 虚表项不在 d3dx9_43 内，拒绝调用
static unsigned long g_cjkTrapped    = 0;   // 绘制/建字体时踩到结构化异常（已被 SEH 兜住）
static int           g_disabled      = 0;   // 逃生开关（chs_off.txt）
static int           g_disabledChk   = 0;

// 文本缓冲环：绘制理论上可能重入触发新的文本绘制，单缓冲会被覆盖，4 槽轮转。
#define CJK_SLOTS 4
#define CJK_BUFSZ 2048
static WCHAR g_wideBuf[CJK_SLOTS][CJK_BUFSZ];
static char  g_gbkBuf [CJK_SLOTS][CJK_BUFSZ];
static char  g_lineBuf[CJK_SLOTS][CJK_BUFSZ];
static volatile long g_slot = 0;

// 逃生开关：<update>\chs_off.txt 存在就完全不接管 CJK。
// 探测 DLL 跑在游戏主循环里，一旦新方案在用户机器上炸了又没开关，
// 用户只能整目录删掉才能进游戏 —— 留一条不重建 DLL 的后路。
static void CjkCheckKillSwitch()
{
    if (g_disabledChk) return;
    g_disabledChk = 1;
    if (!g_logPath[0]) return;
    char p[MAX_PATH];
    int i = 0;
    for (; i < MAX_PATH - 1 && g_logPath[i]; ++i) p[i] = g_logPath[i];
    p[i] = 0;
    // 把 "...\chs_probe.log" 换成 "...\chs_off.txt"
    for (int k = i - 1; k >= 0; --k) {
        if (p[k] == '\\') { p[k + 1] = 0; break; }
    }
    // ★ 用通配符匹配 chs_off* —— 只认死名字 chs_off.txt 会漏掉
    //   用户手建时被记事本加成 chs_off.txt.txt 的情况（第 15 轮实测踩到）。
    my_strcat(p, MAX_PATH, "chs_off*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(p, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        FindClose(h);
        g_disabled = 1;
        LogPrintf("[cjk] KILL SWITCH: found \"%s\" -> CJK takeover DISABLED "
                  "(rename it away from chs_off* to re-enable)\r\n", fd.cFileName);
        FlushFileBuffers(g_log);
    } else {
        LogPrintf("[cjk] kill switch: no chs_off* in update dir -> takeover ACTIVE\r\n");
        FlushFileBuffers(g_log);
    }
}

// 在日志文件同一目录拼一个旁路文件名（逃生开关 / 编码配置都用它）
static void CjkSidePath(const char* name, char* out, size_t cap)
{
    size_t i = 0;
    for (; i + 1 < cap && g_logPath[i]; ++i) out[i] = g_logPath[i];
    out[i] = 0;
    for (size_t k = i; k-- > 0; ) {
        if (out[k] == '\\') { out[k + 1] = 0; break; }
    }
    my_strcat(out, cap, name);
}

// 源文本编码。
// ★ 默认 **GBK（CP_ACP）**，因为部署脚本 tools/make_gbk_deploy.py 写进
//   游戏 Text\ 的就是 GB18030。
//   之前写成"先试 UTF-8，失败再按 GBK"，是个真 bug：GBK 双字节里
//   0xB0 0xA1 这种组合恰好是**合法 UTF-8 序列**（会解成西里尔字母 U+0421），
//   只要整串碰巧都是合法 UTF-8，就会被当成 UTF-8 解出乱码。
//   要换 UTF-8 部署，在 update\ 下放一个 chs_utf8.txt 即可。
// ★ 不能用 `if (g_srcCP)` 当"已决定"判据 —— **CP_ACP 的值就是 0**！
//   写成 g_srcCP = CP_ACP 之后每次调用都会重走一遍并重复打日志
//   （第三轮日志里 `[cjk] source encoding = ...` 刷了满屏就是这个）。
//   另立一个布尔标志。
static int g_srcCP      = 0;
static int g_cpDecided  = 0;

static int CjkSourceCP()
{
    if (g_cpDecided) return g_srcCP;
    g_cpDecided = 1;
    char p[MAX_PATH];
    CjkSidePath("chs_utf8.txt", p, sizeof(p));
    if (p[0] && GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) {
        g_srcCP = CP_UTF8;
        LogPrintf("[cjk] source encoding = UTF-8 (chs_utf8.txt found)\r\n");
    } else {
        g_srcCP = CP_ACP;      // 中文 Windows = 936 = GBK
        LogPrintf("[cjk] source encoding = CP_ACP/GBK (default)\r\n");
    }
    FlushFileBuffers(g_log);
    return g_srcCP;
}

// 校验这真的是 IDirect3DDevice9：虚表必须落在 d3d9.dll 映像范围内。
// ★ 没有这一步，把野指针喂给 D3DXCreateFontA 就是进程内一声闷响。
//
// ★★ 踩过的坑（2026-10-03 第三轮，整整浪费了一轮实机）：
//     void** vt = (void**)dev;      ← 错！dev 是**对象**，不是虚表。
//     这样 vt[0] = 虚表指针，vt[1..3] = 对象自己的数据成员（堆地址），
//     必然不在 d3d9.dll 范围内 → 永远返回 0 → noDev 一路涨、drawn 恒 0。
//     正确写法与 VtblReplace / CjkScanDevice 一致：先解一层拿虚表。
static int LooksLikeDevice(void* dev)
{
    if (!dev || IsBadReadPtr(dev, 4)) return 0;
    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    if (!d3d9) return 0;
    unsigned long base = (unsigned long)d3d9;
    unsigned long size = PEImageSize(d3d9);
    if (!size) return 0;

    void** vt = *(void***)dev;                 // ① 解一层才是 vtable
    if (!vt || IsBadReadPtr(vt, 0x30)) return 0;   // 只需读前 4 项
    for (int i = 0; i < 4; ++i) {
        unsigned long v = (unsigned long)vt[i];
        if (v < base || v >= base + size) return 0;
    }
    return 1;
}

// ================================================================ D3D 设备捕获
//
// ★★ 为什么必须自己抓设备（这是 2026-10-03 第二轮实机的直接结论）：
//   原方案是借引擎字形渲染器的设备 —— [[screen+0xE34] + 0x5C]。
//   实机日志打脸：**noDev=10499**，一次都没拿到，画面上依旧是俄文字形。
//   根因：screen+0xE34 恒为 0 —— 那个对象是**懒创建**的，而本作从来没走过
//   sub_4306A0 那条通路（统计段 fontType 恒 >126），所以它压根没被 new 出来。
//
//   改走"源头捕获"：
//     ① 改写 exe 导入表里的 Direct3DCreate9 槽（RVA 0x0D9314）
//        → 我们的钩子拿到 IDirect3D9*
//     ② 把 IDirect3D9 虚表的 CreateDevice（索引 16，偏移 0x40）换掉
//        → 拿到 IDirect3DDevice9*
//     ③ 换掉设备虚表的 EndScene（索引 42，偏移 0xA8）→ 在场景内刷绘制队列
//
//   时序上没问题：ASI loader 在进程初始化阶段就加载本 DLL，早于游戏 WinMain。
//
// ★ 为什么改 IAT 而不是 inline hook 目标函数：
//   导入表槽是一次性 4 字节写入，不用算指令边界、不用搬移被截断的原指令，
//   也不存在"别的线程正执行到被改写指令中间"的风险。

typedef void* (__stdcall *Direct3DCreate9_t)(unsigned long SDKVersion);
typedef long  (__stdcall *CreateDevice_t)(void* self, unsigned long Adapter,
                                          unsigned long DeviceType, void* hFocusWindow,
                                          unsigned long BehaviorFlags,
                                          void* pPresentationParameters,
                                          void** ppReturnedDeviceInterface);
typedef long  (__stdcall *Reset_t)    (void* self, void* pPresentationParameters);
typedef long  (__stdcall *EndScene_t) (void* self);
// IDirect3DDevice9::Present —— 虚表索引 17（0x44）
typedef long  (__stdcall *Present_t)  (void* self, const void* pSourceRect,
                                       const void* pDestRect, void* hDestWindowOverride,
                                       const void* pDirtyRegion);

static Direct3DCreate9_t g_origCreate9      = NULL;
static CreateDevice_t    g_origCreateDevice = NULL;
static Reset_t           g_origReset        = NULL;
static EndScene_t        g_origEndScene     = NULL;
static Present_t         g_origPresent      = NULL;
static void*             g_d3d9obj          = NULL;   // IDirect3D9*
static void*             g_device           = NULL;   // IDirect3DDevice9*
static unsigned long     g_endSceneCalls    = 0;
static unsigned long     g_presentCalls     = 0;
static unsigned long     g_swapPresentCalls = 0;
static volatile long     g_presentSeen      = 0;   // 见过一次 Present ⇒ 交给它刷批次

// 这些定义在文件更后方，这里先声明（HookPresent / HookCreateDevice 要用）
static int  InD3d9(const void* fn);
extern "C" long __cdecl CjkSafeCall3(void*, void*, void*, void*);
static long ManDrawQuads(void* dev);
static unsigned long     g_resetCalls       = 0;

// 前向声明：字体缓存与绘制队列定义在下方「D3DX 字体」一节
static void  CjkFlush();
static void  CjkVerifyFont(void* dev, void* f, int heightPx);
static long  CjkMeasureW(void* font, const WCHAR* s, int* widthOut);
static void  CjkFontsOnLost();
static void  CjkFontsOnReset();

// 替换某个 COM 对象虚表的第 idx 项，返回原值。
// ★ vtable 位于 d3d9.dll 的 .rdata，默认只读 —— 必须先 VirtualProtect。
static void* VtblReplace(void* obj, int idx, void* newFn)
{
    if (!obj || IsBadReadPtr(obj, 4)) return NULL;
    void** vt = *(void***)obj;
    if (!vt || IsBadReadPtr(vt, (idx + 1) * 4)) return NULL;

    DWORD oldp = 0;
    if (!VirtualProtect(&vt[idx], 4, PAGE_EXECUTE_READWRITE, &oldp)) return NULL;
    void* old = vt[idx];
    vt[idx] = newFn;
    DWORD junk = 0;
    VirtualProtect(&vt[idx], 4, oldp, &junk);
    return old;
}

static void  CjkDropDeviceResources(void);

// ================================================================ 帧节奏探针
//
// ★ 第 19 轮：为了判断"一个界面只显示一行字"到底是
//   ① 我们的顶点数据/纹理被折叠，还是
//   ② 引擎在**每个文本项之间**清屏 / 重设视口
//   直接给三个最关键的槽位加计数。索引来源与 41/42/45/48 同一条链
//   （57 SetRenderState / 59 CreateStateBlock / 65 SetTexture / 67 SetTSS /
//    69 SetSamplerState / 89 SetFVF / 107 SetVertexShader 均已实测）：
//       41 BeginScene  42 EndScene  43 Clear  44 SetTransform  45 GetTransform
//       46 MultiplyTransform  47 SetViewport  48 GetViewport
static unsigned long g_beginSceneCalls = 0;
static unsigned long g_clearCalls      = 0;
static unsigned long g_setVpCalls      = 0;

typedef long (__stdcall *BeginScene_t)(void*);
typedef long (__stdcall *Clear_t)(void*, unsigned long, const void*, unsigned long,
                                  unsigned long, float, unsigned long);
typedef long (__stdcall *SetViewport_t)(void*, const void*);

static BeginScene_t  g_origBeginScene  = NULL;
static Clear_t       g_origClear       = NULL;
static SetViewport_t g_origSetViewport = NULL;

static long __stdcall HookBeginScene(void* self)
{
    ++g_beginSceneCalls;
    return g_origBeginScene ? g_origBeginScene(self) : -1;
}

static long __stdcall HookClear(void* self, unsigned long count, const void* rects,
                                unsigned long flags, unsigned long color,
                                float z, unsigned long stencil)
{
    ++g_clearCalls;
    if (g_clearCalls <= 8) {
        LogPrintf("[frame] Clear #%lu count=%lu flags=0x%lX color=0x%08lX\r\n",
                  g_clearCalls, count, flags, color);
        FlushFileBuffers(g_log);
    }
    return g_origClear ? g_origClear(self, count, rects, flags, color, z, stencil) : -1;
}

struct ManVP2 { unsigned long x, y, w, h; float zmin, zmax; };
static long __stdcall HookSetViewport(void* self, const void* pvp)
{
    ++g_setVpCalls;
    if (g_setVpCalls <= 8 && pvp) {
        const ManVP2* v = (const ManVP2*)pvp;
        LogPrintf("[frame] SetViewport #%lu -> %lux%lu @(%lu,%lu)\r\n",
                  g_setVpCalls, v->w, v->h, v->x, v->y);
        FlushFileBuffers(g_log);
    }
    return g_origSetViewport ? g_origSetViewport(self, pvp) : -1;
}

// IDirect3DDevice9::Reset —— 设备重置会毁掉 D3DX 字体的内部资源，
// 必须在原始 Reset 前后分别调 OnLostDevice / OnResetDevice。
// ★ 纹理 / 状态块 / 槽位探测结果的作废，统一放在 Reset 钩子链里
//   （见 CjkOnDeviceLost），这里不直接碰那些变量。
static long __stdcall HookReset(void* self, void* pPresentationParameters)
{
    ++g_resetCalls;
    CjkFontsOnLost();
    // ★ 设备 Reset 后纹理/状态块/槽位探测结果都属于旧设备，必须作废。
    //   否则 CjkFlush 会拿着野指针去画 —— 第 10 轮崩在 `dvt[57]` 就是这类问题。
    CjkDropDeviceResources();

    long hr = g_origReset ? g_origReset(self, pPresentationParameters) : -1;
    CjkFontsOnReset();
    if (g_resetCalls <= 4) {
        LogPrintf("[d3d] Reset #%lu -> hr=0x%08lX (tex+stateblock dropped, will rebuild)\r\n",
                  g_resetCalls, (unsigned long)hr);
        FlushFileBuffers(g_log);
    }
    return hr;
}

// IDirect3DDevice9::EndScene —— 此刻仍在场景内，是把 CJK 队列画掉的时机。
// ★ 为什么不在 CjkDispatch 里直接画：D3DX 字体内部用 ID3DXSprite，
//   它需要设备处于有效渲染状态。放在 EndScene 之前刷队列是最稳的落点。
static long __stdcall HookEndScene(void* self)
{
    ++g_endSceneCalls;
    CjkFlush();
    return g_origEndScene ? g_origEndScene(self) : -1;
}

// IDirect3DDevice9::Present —— ★ 本作实测根本不调 EndScene（第三轮实机
//   endScene=0 跑了 8000 次绘制），所以 Present 才是唯一确定会来的落点。
//   在原始 Present **之前**把队列画掉，这样中文随这一帧一起被提交。
//   （EndScene 若也被调用，队列那时已清空，这里自然变成空操作。）
// IDirect3DSwapChain9::Present —— 实测设备 Present 从不被调用，本作的帧末
// 很可能是走交换链的。两个都挂上，谁先来谁刷批次。
typedef long (__stdcall *SwapPresent_t)(void*, const void*, const void*, void*, const void*);
static SwapPresent_t g_origSwapPresent = NULL;
static void*         g_swapChain       = NULL;

// ================================================================ 帧末钩子（第 22 轮核心改造）
//
// ★★★ 为什么需要它：
//   实测（对照实验）本作这个 D3D9 实现下，**被别的绘制隔开的 DrawPrimitiveUP
//   只有最后一笔能落屏**。菜单里文本恰好是最后画的所以看得见；游戏内 HUD 的文本
//   在**世界之后**被盖住 ⇒ 表现就是"看不见 / 闪"。
//   ⇒ 正解：把我们的绘制统一放到**引擎一帧画完之后、Present 之前**，一帧只画一次。
//
// 引擎帧末这段（IDA 逐条核对，RVA）：
//   3041E  mov eax, [edi+0E28h]      ; edi = 引擎 D3D 包装对象(dword_502AD4)
//   30424  lea edx, [ebp-14h]
//   30427  push 0 / 30429 push 0 / 3042B push edx
//   3042C  mov ecx, [eax]            ; ecx = 设备虚表
//   3042E  lea edx, [ebp-24h]
//   30431  push edx / 30432 push eax
//   30433  call dword ptr [ecx+44h]  ; Present
// 0x3041E 那条指令正好 **6 字节**（8B 87 28 0E 00 00）⇒ 整条换成 jmp rel32(5) + nop。
//
// 为什么不用虚表钩子：我们确实 hook 了设备 Present（orig 是 d3d9 真函数），
//   但 present 计数恒为 0 —— 引擎 Present 走的是 [screen+0xE28] 那个对象，
//   与我们 IAT 捕获的设备**不是同一个**。在代码上钉死才可靠。
static void*         g_frameResume    = NULL;
static int           g_frameHookOk    = 0;
static unsigned long g_frameEndCalls  = 0;
static unsigned long g_frameDrawnQuads= 0;
static void*         g_frameDevLogged = NULL;
static unsigned long g_lastFrameTick  = 0;

extern "C" void __cdecl ManFrameEndFlush(void* engineScreen);      // 定义在文件下方
// ★ 只做普通声明：MSVC 不允许在**声明**上加 __declspec(naked)（C2488），
//   naked 必须写在定义上。
extern "C" void CjkFrameEndTrampoline(void);

// 复原我们盖掉的那条指令，然后跳回引擎代码
extern "C" __declspec(naked) void CjkFrameEndResumeThunk(void)
{
    __asm { jmp dword ptr [g_frameResume] }
}

extern "C" __declspec(naked) void CjkFrameEndTrampoline(void)
{
    __asm {
        pushad
        pushfd
        push edi                  // 参数 = 引擎的 D3D 包装对象
        call ManFrameEndFlush
        add  esp, 4
        popfd
        popad
        mov  eax, [edi+0E28h]     // ★ 复原被覆盖的原指令
        jmp  CjkFrameEndResumeThunk
    }
}

static void CjkInstallFrameHook(void)
{
    if (g_frameHookOk || !g_exe) return;
    BYTE* p = (BYTE*)g_exe + RVA_FrameEndPatch;
    static const unsigned char expect[6] = { 0x8B, 0x87, 0x28, 0x0E, 0x00, 0x00 };
    if (IsBadReadPtr(p, 6)) {
        LogPrintf("[frame] patch site unreadable -> frame hook skipped\r\n");
        FlushFileBuffers(g_log);
        return;
    }
    for (int i = 0; i < 6; ++i) {
        if (p[i] != expect[i]) {
            LogPrintf("[frame] unexpected bytes at exe+0x%lX: "
                      "%02X %02X %02X %02X %02X %02X -> frame hook skipped\r\n",
                      (unsigned long)RVA_FrameEndPatch, p[0], p[1], p[2], p[3], p[4], p[5]);
            FlushFileBuffers(g_log);
            return;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old)) {
        LogPrintf("[frame] VirtualProtect failed, err=%lu -> frame hook skipped\r\n", GetLastError());
        FlushFileBuffers(g_log);
        return;
    }
    g_frameResume = (void*)((BYTE*)g_exe + RVA_FrameEndNext);
    BYTE* h = (BYTE*)CjkFrameEndTrampoline;
    p[0] = 0xE9;
    *(long*)(p + 1) = (long)(h - (p + 5));
    p[5] = 0x90;
    VirtualProtect(p, 6, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 6);
    g_frameHookOk = 1;
    LogPrintf("[frame] frame-end hook installed: exe+0x%lX -> %p, resume=%p\r\n",
              (unsigned long)RVA_FrameEndPatch, h, g_frameResume);
    FlushFileBuffers(g_log);
}

static long __stdcall HookPresent(void* self, const void* a, const void* b,
                                  void* c, const void* d)
{
    ++g_presentCalls;
    g_presentSeen = 1;
    ManDrawQuads(g_device);      // 帧末统一画（状态只设/还原一次）
    return g_origPresent ? g_origPresent(self, a, b, c, d) : -1;
}

static long __stdcall HookSwapPresent(void* self, const void* a, const void* b,
                                      void* c, const void* d)
{
    ++g_swapPresentCalls;
    g_presentSeen = 1;
    ManDrawQuads(g_device);
    return g_origSwapPresent ? g_origSwapPresent(self, a, b, c, d) : -1;
}

// IDirect3D9::CreateDevice —— 设备在这里诞生
static long __stdcall HookCreateDevice(void* self, unsigned long Adapter,
                                       unsigned long DeviceType, void* hFocusWindow,
                                       unsigned long BehaviorFlags,
                                       void* pPresentationParameters,
                                       void** ppReturnedDeviceInterface)
{
    long hr = -1;
    if (g_origCreateDevice)
        hr = g_origCreateDevice(self, Adapter, DeviceType, hFocusWindow,
                                BehaviorFlags, pPresentationParameters,
                                ppReturnedDeviceInterface);

    if (hr >= 0 && ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
        g_device       = *ppReturnedDeviceInterface;
        g_origReset    = (Reset_t)   VtblReplace(g_device, 16, (void*)HookReset);
        g_origEndScene = (EndScene_t)VtblReplace(g_device, 42, (void*)HookEndScene);
        g_origPresent  = (Present_t) VtblReplace(g_device, 17, (void*)HookPresent);
        // 帧节奏探针（只计数，不改行为）
        g_origBeginScene  = (BeginScene_t) VtblReplace(g_device, 41, (void*)HookBeginScene);
        g_origClear       = (Clear_t)      VtblReplace(g_device, 43, (void*)HookClear);
        g_origSetViewport = (SetViewport_t)VtblReplace(g_device, 47, (void*)HookSetViewport);
        // 取隐式交换链并挂 Present（槽位 14 取链、3 为 Present）
        {
            void** dvt0 = *(void***)g_device;
            void*  gsc  = (dvt0 && !IsBadReadPtr(dvt0, 0x40)) ? dvt0[14] : NULL;
            if (gsc && InD3d9(gsc)) {
                void* sc = NULL;
                long shr = CjkSafeCall3(gsc, g_device, NULL, &sc);
                if (shr == 0 && sc && !IsBadReadPtr(sc, 0x10)) {
                    void** svt = *(void***)sc;
                    if (!IsBadReadPtr(svt, 0x20) && InD3d9(svt[2]) && InD3d9(svt[3])) {
                        g_swapChain = sc;
                        g_origSwapPresent = (SwapPresent_t)VtblReplace(sc, 3, (void*)HookSwapPresent);
                        LogPrintf("[d3d] swapchain=%p Present orig=%p\r\n",
                                  sc, (void*)g_origSwapPresent);
                    } else {
                        LogPrintf("[d3d] swapchain vtable looks wrong -> skip\r\n");
                    }
                } else {
                    LogPrintf("[d3d] GetSwapChain hr=0x%08lX sc=%p -> skip\r\n",
                              (unsigned long)shr, sc);
                }
            } else {
                LogPrintf("[d3d] GetSwapChain slot 14 not in d3d9 -> skip\r\n");
            }
        }
        FlushFileBuffers(g_log);
        CjkInstallFrameHook();     // ★ 帧末绘制落点（改 exe 6 字节代码）
        LogPrintf("[d3d] CreateDevice -> dev=%p looksOK=%d\r\n"
                  "[d3d]   Reset orig=%p  EndScene orig=%p  Present orig=%p\r\n",
                  g_device, LooksLikeDevice(g_device),
                  (void*)g_origReset, (void*)g_origEndScene, (void*)g_origPresent);
    } else {
        LogPrintf("[d3d] CreateDevice FAILED hr=0x%08lX\r\n", (unsigned long)hr);
    }
    FlushFileBuffers(g_log);
    return hr;
}

static void* __stdcall HookDirect3DCreate9(unsigned long SDKVersion)
{
    void* p = g_origCreate9 ? g_origCreate9(SDKVersion) : NULL;
    g_d3d9obj = p;
    if (p) {
        g_origCreateDevice = (CreateDevice_t)VtblReplace(p, 16, (void*)HookCreateDevice);
        LogPrintf("[d3d] Direct3DCreate9(%lu) -> %p ; CreateDevice orig=%p\r\n",
                  SDKVersion, p, (void*)g_origCreateDevice);
    } else {
        LogPrintf("[d3d] Direct3DCreate9(%lu) -> NULL\r\n", SDKVersion);
    }
    FlushFileBuffers(g_log);
    return p;
}

static void InstallD3DCapture()
{
    HMODULE m = GetModuleHandleA("d3d9.dll");
    if (!m) m = LoadLibraryA("d3d9.dll");
    Direct3DCreate9_t real = m ? (Direct3DCreate9_t)GetProcAddress(m, "Direct3DCreate9") : NULL;

    void** iat = (void**)((BYTE*)g_exe + RVA_IAT_D3DCREATE9);
    if (IsBadReadPtr(iat, 4)) {
        LogPrintf("[d3d] IAT slot (RVA 0x%06X) unreadable -> capture NOT installed\r\n",
                  RVA_IAT_D3DCREATE9);
        return;
    }

    // 原值优先取 IAT 里现有的（loader 已经填好的真实地址）
    void* cur = *iat;
    g_origCreate9 = (Direct3DCreate9_t)(cur ? cur : (void*)real);
    if (!g_origCreate9) {
        LogPrintf("[d3d] no Direct3DCreate9 -> capture NOT installed\r\n");
        return;
    }

    DWORD oldp = 0;
    if (!VirtualProtect(iat, 4, PAGE_READWRITE, &oldp)) {
        LogPrintf("[d3d] VirtualProtect(IAT) failed err=%lu\r\n", GetLastError());
        return;
    }
    *iat = (void*)HookDirect3DCreate9;
    DWORD junk = 0;
    VirtualProtect(iat, 4, oldp, &junk);

    LogPrintf("[d3d] IAT Direct3DCreate9 hooked: %p -> %p (real export=%p)\r\n",
              (void*)g_origCreate9, (void*)HookDirect3DCreate9, (void*)real);
    FlushFileBuffers(g_log);
}

// 兜底扫描：在 screen 对象里找一个"虚表完整落在 d3d9.dll 里"的指针。
// ★ 判别标准取自 IDirect3DDevice9 的虚表规模 = 119 项：
//   抽查索引 0 / 41(BeginScene) / 42(EndScene) / 118 都在 d3d9.dll 映像内。
//   IDirect3D9 只有 17 项，索引 41 之外必然越界 → 会被这条判据排除。
//   找到就顺便把偏移记进日志，下次可以直接硬编码。
static void* CjkScanDevice(void* screen, int* offOut)
{
    if (!screen || IsBadReadPtr(screen, 0x10)) return NULL;
    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    if (!d3d9) return NULL;
    unsigned long base = (unsigned long)d3d9;
    unsigned long size = PEImageSize(d3d9);
    if (!size) return NULL;

    for (unsigned long o = 0xE00; o <= 0xF00; o += 4) {
        const void* slot = (const char*)screen + o;
        if (IsBadReadPtr(slot, 4)) continue;
        void* p = *(void* const*)slot;
        if (!p || IsBadReadPtr(p, 4)) continue;
        void** vt = *(void***)p;
        if (!vt || IsBadReadPtr(vt, 0x200)) continue;

        static const int kProbe[4] = { 0, 41, 42, 118 };
        int ok = 1;
        for (int k = 0; k < 4; ++k) {
            unsigned long v = (unsigned long)vt[kProbe[k]];
            if (v < base || v >= base + size) { ok = 0; break; }
        }
        if (!ok) continue;
        if (offOut) *offOut = (int)o;
        return p;
    }
    return NULL;
}

// 取 D3D 设备：
//   ① 优先用 IAT+虚表钩子捕获到的 g_device（唯一实测可靠的来源）
//   ② 回退：[[screen+0xE34] + 0x5C]（引擎自己的字形渲染器，本作恒为 0）
//   ③ 最后兜底：扫 screen+0xE00..0xF00 找虚表落在 d3d9.dll 的指针
static void* CjkGetDevice(void** trOut)
{
    if (trOut) *trOut = NULL;

    if (g_device && LooksLikeDevice(g_device)) {
        // 顺带把 tr 取出来（只用于读缩放系数，拿不到也不影响）
        if (trOut && g_exe && !IsBadReadPtr(g_exe, 4)) {
            void** pScreen = (void**)((BYTE*)g_exe + RVA_GlobalScreen);
            if (!IsBadReadPtr(pScreen, 4) && *pScreen && !IsBadReadPtr(*pScreen, 0x40)) {
                void** pTR = (void**)((char*)*pScreen + OFF_TextRenderer);
                if (!IsBadReadPtr(pTR, 4) && *pTR && !IsBadReadPtr(*pTR, OFF_TR_Device + 4))
                    *trOut = *pTR;
            }
        }
        return g_device;
    }

    if (!g_exe || IsBadReadPtr(g_exe, 4)) return NULL;

    void** pScreen = (void**)((BYTE*)g_exe + RVA_GlobalScreen);
    if (IsBadReadPtr(pScreen, 4)) return NULL;
    void* screen = *pScreen;
    if (!screen || IsBadReadPtr(screen, 0x40)) return NULL;

    void** pTR = (void**)((char*)screen + OFF_TextRenderer);
    if (IsBadReadPtr(pTR, 4)) return NULL;
    void* tr = *pTR;
    if (!tr || IsBadReadPtr(tr, OFF_TR_Device + 4)) return NULL;

    void* dev = *(void**)((char*)tr + OFF_TR_Device);
    if (!dev || IsBadReadPtr(dev, 0x40)) return NULL;

    if (trOut) *trOut = tr;
    return dev;
}

// 兜底路径（仅在 ①② 都失败时调用一次，结果记进日志）
static int  g_scanLogged = 0;
static void* CjkGetDeviceFallback()
{
    if (!g_exe || IsBadReadPtr(g_exe, 4)) return NULL;
    void** pScreen = (void**)((BYTE*)g_exe + RVA_GlobalScreen);
    // ★ 早退也要留痕：上一轮实机日志里完全没有 fallback scan 这一行，
    //   当时无法区分"screen 为空"还是"扫了但没找到"，白猜了一次。
    if (IsBadReadPtr(pScreen, 4) || !*pScreen) {
        if (!g_scanLogged) {
            g_scanLogged = 1;
            LogPrintf("[d3d] fallback scan: screen(RVA 0x%06X) = %s -> abort\r\n",
                      RVA_GlobalScreen,
                      IsBadReadPtr(pScreen, 4) ? "(unreadable)" : "0");
            FlushFileBuffers(g_log);
        }
        return NULL;
    }

    int off = -1;
    void* dev = CjkScanDevice(*pScreen, &off);
    if (!g_scanLogged) {
        g_scanLogged = 1;
        LogPrintf("[d3d] fallback scan: screen=%p -> dev=%p\r\n", *pScreen, dev);
        if (off >= 0)
            LogPrintf("[d3d] fallback scan: found at screen+0x%X\r\n", (unsigned long)off);
        else
            LogPrintf("[d3d] fallback scan: NOT found in screen+0xE00..0xF00\r\n");
        FlushFileBuffers(g_log);
    }
    return dev;
}

// UTF-8 → 系统 ANSI(中文 Windows = GBK)。
// ★ 输入若已经不是合法 UTF-8，就认为它本来就是 GBK，原样返回。
static const char* CjkMakeAnsi(const char* text, int nbytes)
{
    if (!text || nbytes <= 0) return NULL;

    int nWide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, nbytes, NULL, 0);
    if (nWide <= 0) return text;              // 不是 UTF-8 → 当作已是 GBK
    if (nWide > CJK_BUFSZ - 1) nWide = CJK_BUFSZ - 1;

    long slot = (InterlockedIncrement(&g_slot) - 1) & (CJK_SLOTS - 1);
    WCHAR* w = g_wideBuf[slot];
    int wGot = MultiByteToWideChar(CP_UTF8, 0, text, nbytes, w, nWide);
    if (wGot <= 0) return NULL;
    if (wGot > CJK_BUFSZ - 1) wGot = CJK_BUFSZ - 1;
    w[wGot] = 0;                              // 显式长度调用不会自动补终止符

    char* a = g_gbkBuf[slot];
    int aGot = WideCharToMultiByte(CP_ACP, 0, w, wGot, a, CJK_BUFSZ - 1, NULL, NULL);
    if (aGot <= 0) return NULL;
    a[aGot] = 0;
    return a;
}

// ---------------------------------------------------------------- D3DX 字体
// d3dx9_43.dll 由游戏自身加载（导入表确认），导出了 D3DXCreateFontA。
// ★★ ID3DXFont 虚表（d3dx9core.h 标准布局，下面是**索引**不是字节偏移）
//    0 QueryInterface   1 AddRef        2 Release
//    3 GetDevice        4 GetDescA      5 GetDescW
//    6 GetTextMetricsA  7 GetTextMetricsW
//    8 GetDC            9 GetGlyphData
//   10 PreloadGlyphs   11 PreloadTextA 12 PreloadTextW
//   13 DrawTextA       14 DrawTextW
//   15 OnLostDevice    16 OnResetDevice
//
// ★ 第五轮实机就翻在这（2026-10-03）：旧代码按"GetDescW 之后紧接着就是
//   DrawText"写成 8 / 9 / 10 / 11，漏掉了 GetDC / GetGlyphData /
//   PreloadGlyphs / PreloadTextA / PreloadTextW 整整五项。于是 vt[9]
//   实际上是 GetGlyphData：
//       GetGlyphData(font, Glyph=pSprite(NULL), ppTexture=pString,
//                    pBlackBox=Count(-1), pCellInc=pRect)
//   → d3dx9_43 内部解引用 pBlackBox = 0xFFFFFFFF，当场 AV。
//   实测现场完全吻合：EIP 在 d3dx9_43.dll 内、EDI=FFFFFFFF、
//   fault=FFFFFFFF READ、EAX=0（Glyph=NULL）。
typedef long (__stdcall *D3DXCreateFontA_t)(void*   device,
                                            int     height,
                                            unsigned long width,
                                            unsigned long weight,
                                            unsigned long mipLevels,
                                            int     italic,
                                            unsigned long charSet,
                                            unsigned long outputPrecision,
                                            unsigned long quality,
                                            unsigned long pitchAndFamily,
                                            const char* faceName,
                                            void**  ppFont);
// ★ 用 DrawTextW 而不是 DrawTextA：我们在外面自己把 GBK → UTF-16，
//   这样"字节当什么编码解释"这件事完全由我们决定，不依赖 D3DX 内部
//   对 ANSI 的处理（实测 D3DX 的 *A 版本会走 CP_ACP，但显式转换更可控）。
typedef long (__stdcall *DrawTextW_t)(void* self, void* sprite, const WCHAR* str,
                                      int count, RECT* rect, unsigned long format,
                                      unsigned long color);

// ★ GB2312_CHARSET / FW_NORMAL / ANTIALIASED_QUALITY / DEFAULT_PITCH 都在
//   wingdi.h 里定义过了 —— 这里再写一遍会刷 C4005 重定义警告，直接用系统的。
//   DT_* 则加下划线后缀另起名字，避免和 winuser.h 冲突。
#define D3DX_FONT_VTBL_GETDEVICE  3
#define D3DX_FONT_VTBL_GETDESCW   5
#define D3DX_FONT_VTBL_DRAWTEXTW 14
#define D3DX_FONT_VTBL_ONLOST    15
#define D3DX_FONT_VTBL_ONRESET   16

#define DT_SINGLELINE_            0x00000020u
#define DT_NOCLIP_                0x00000100u
#define DT_NOPREFIX_              0x00000800u
#define DT_CALCRECT_              0x00000400u

static HMODULE           g_d3dx        = NULL;
static D3DXCreateFontA_t g_pCreateFont = NULL;
static int               g_fontHeight  = 0;
static long              g_fontHr      = -1;
static int               g_fontVerified = 0;

// ---- 字体缓存 ----
// 不同字号各建一个。菜单里通常只有 1~2 种字号，缓存能避免每帧重建字体。
#define FONT_CACHE 4
static void* g_fontObj[FONT_CACHE];
static void* g_fontDev[FONT_CACHE];
static int   g_fontHpx[FONT_CACHE];

// ---------------------------------------------------------------- SEH 兜底
// ★ 从第四轮的崩溃学到的：我们的绘制是**寄生在游戏的渲染循环里**的，
//   一旦里面踩到异常，哪怕只是一次，游戏进程就没了，一行线索都留不下。
//   所以凡是"调用别人的函数指针"的地方（D3DXCreateFontA / DrawTextW）
//   一律用 __try/__except 包起来：
//     · 记下异常码 + 出错地址 + 故障地址（读写、哪个地址）
//     · 连续踩雷满 8 次就自动关闭 CJK 接管，把画面还给原位图通路
//   → 最坏情况是"中文没画出来"，绝不会是"游戏崩了"。
static int CjkTrap(const char* what, EXCEPTION_POINTERS* ep)
{
    g_cjkTrapped++;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    void* eip  = ep->ExceptionRecord->ExceptionAddress;
    void* fa   = NULL;
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        fa = (void*)ep->ExceptionRecord->ExceptionInformation[1];

    if (g_cjkTrapped <= 5) {
        LogPrintf("[cjk] SEH trap in %s: code=0x%08lX eip=%p fault=%p (#%lu)\r\n",
                  what, (unsigned long)code, eip, fa, g_cjkTrapped);
        FlushFileBuffers(g_log);
    }
    if (g_cjkTrapped == 8) {
        g_disabled = 1;
        LogWrite("[cjk] too many traps -> TAKEOVER DISABLED (game keeps running)\r\n");
        FlushFileBuffers(g_log);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// ★ 惰性创建：DllMain 阶段设备根本还没创建，那时建字体必然失败。
//   现在由 IAT 钩子拿到真实设备后再建，见 CjkFontFor。
static void* CjkCreateFont(void* dev, int heightPx)
{
    if (!g_d3dx) {
        g_d3dx = GetModuleHandleA("d3dx9_43.dll");
        if (!g_d3dx) g_d3dx = LoadLibraryA("d3dx9_43.dll");
        if (g_d3dx) g_pCreateFont = (D3DXCreateFontA_t)GetProcAddress(g_d3dx, "D3DXCreateFontA");
        if (!g_pCreateFont) {
            LogPrintf("[cjk] D3DXCreateFontA not found (d3dx9_43=%p)\r\n", (void*)g_d3dx);
            FlushFileBuffers(g_log);
        }
    }
    if (!g_pCreateFont) return NULL;

    void* nf = NULL;
    long hr = -1;
    __try {
        hr = g_pCreateFont(dev, heightPx, 0, FW_NORMAL, 1, 0,
                           GB2312_CHARSET, 0, ANTIALIASED_QUALITY,
                           DEFAULT_PITCH, "SimSun", &nf);
    } __except (CjkTrap("D3DXCreateFontA", GetExceptionInformation())) {
        hr = -2;
        nf = NULL;
    }

    g_fontHr     = hr;
    g_fontHeight = heightPx;
    LogPrintf("[cjk] D3DXCreateFontA(dev=%p, h=%d, cs=%d) -> hr=0x%08lX font=%p\r\n",
              dev, heightPx, (int)GB2312_CHARSET, (unsigned long)hr, nf);
    FlushFileBuffers(g_log);

    if (hr != 0 || !nf) return NULL;

    // 只验第一次建出来的字体；索引对不对，一次就够判断
    if (!g_fontVerified) {
        g_fontVerified = 1;
        CjkVerifyFont(dev, nf, heightPx);
    }
    return nf;
}

static void* CjkFontFor(void* dev, int heightPx)
{
    for (int i = 0; i < FONT_CACHE; ++i)
        if (g_fontObj[i] && g_fontDev[i] == dev && g_fontHpx[i] == heightPx)
            return g_fontObj[i];

    void* f = CjkCreateFont(dev, heightPx);
    if (!f) return NULL;

    int slot = -1;
    for (int i = 0; i < FONT_CACHE; ++i) if (!g_fontObj[i]) { slot = i; break; }
    if (slot < 0) slot = FONT_CACHE - 1;   // 满了就顶掉最后一个（字号 >4 种才会发生）

    g_fontObj[slot] = f;
    g_fontDev[slot] = dev;
    g_fontHpx[slot] = heightPx;
    return f;
}

// 设备 Reset 前后必须通知 D3DX 字体，否则内部资源失效、后面再也画不出东西
static void CjkFontsOnLost()
{
    for (int i = 0; i < FONT_CACHE; ++i) {
        void* f = g_fontObj[i];
        if (!f || IsBadReadPtr(f, 4)) continue;
        void** vt = *(void***)f;                 // ★ 解一层才是虚表
        if (!vt || IsBadReadPtr(vt, 0x40)) continue;
        typedef long (__stdcall *V_t)(void*);
        V_t fn = (V_t)vt[D3DX_FONT_VTBL_ONLOST];
        if (fn && !IsBadReadPtr((const void*)fn, 1)) fn(f);
    }
}

static void CjkFontsOnReset()
{
    for (int i = 0; i < FONT_CACHE; ++i) {
        void* f = g_fontObj[i];
        if (!f || IsBadReadPtr(f, 4)) continue;
        void** vt = *(void***)f;                 // ★ 解一层才是虚表
        if (!vt || IsBadReadPtr(vt, 0x40)) continue;
        typedef long (__stdcall *V_t)(void*);
        V_t fn = (V_t)vt[D3DX_FONT_VTBL_ONRESET];
        if (fn && !IsBadReadPtr((const void*)fn, 1)) fn(f);
    }
}

// ---------------------------------------------------------------- 绘制队列
// 文本绘制发生在引擎自己的渲染循环里（sub_426680），而 D3DX 字体要靠
// ID3DXSprite 出图。稳妥做法：把请求攒起来，在 EndScene 之前一次性刷掉，
// 那时必定处于有效渲染状态。
// 万一 EndScene 钩子没装上（理论上不会），退化为"入队即刷"。
#define CJKQ_MAX   96
#define CJKQ_CHARS 256

struct CjkItem {
    WCHAR t[CJKQ_CHARS];
    int   x, y, h, nl;      // 锚点坐标 / 字号 / 行数
    int   align;            // Block+0x80 的低 4 位：1=水平居中 2=右对齐 4=底部 8=垂直居中
};
static CjkItem       g_q[CJKQ_MAX];
static volatile long g_qCount     = 0;
static long          g_lastDrawHr = -999;

// 函数指针必须落在 d3dx9_43.dll 映像内才允许 call。
// 索引错一位时这条能把"进程崩溃"降级成"中文没画出来"。
static int InD3dx(const void* fn)
{
    if (!fn || !g_d3dx) return 0;
    unsigned long base = (unsigned long)g_d3dx;
    unsigned long size = PEImageSize(g_d3dx);
    unsigned long v    = (unsigned long)fn;
    return (size && v >= base && v < base + size) ? 1 : 0;
}

// 量一段文本的像素宽度（DT_CALCRECT，不出图）。
// ★ 必须自己量：引擎的居中算法按**字节数**算宽度（sub_426680 逐字节
//   推进 charWidth），汉字一个字占 2 字节，而我们的字形一个字只有
//   fontH 宽 —— 照抄引擎的 linesH 会把整段文字推到屏幕外。
static long CjkMeasureW(void* font, const WCHAR* s, int* widthOut)
{
    if (widthOut) *widthOut = 0;
    if (!font || !s) return -1;
    void** vt = *(void***)font;
    if (!vt || IsBadReadPtr(vt, 0x48)) return -1;

    DrawTextW_t dt = (DrawTextW_t)vt[D3DX_FONT_VTBL_DRAWTEXTW];
    if (!dt || IsBadReadPtr((const void*)dt, 1)) return -1;
    if (!InD3dx(dt)) { g_cjkBadVtbl++; return -2; }

    RECT r;
    r.left = 0; r.top = 0; r.right = 0; r.bottom = 0;

    long hr = -3;
    __try {
        hr = dt(font, NULL, s, -1, &r,
                DT_SINGLELINE_ | DT_NOCLIP_ | DT_NOPREFIX_ | DT_CALCRECT_,
                0xFFFFFFFFul);
    } __except (CjkTrap("ID3DXFont::DrawTextW(CALCRECT)", GetExceptionInformation())) {
        hr = -4;
    }
    if (widthOut) *widthOut = (int)(r.right - r.left);
    return hr;
}

// ★ 字体建成后立刻反查一次虚表布局，别再靠"崩一次才知道索引对不对"。
//   GetDevice(3) 必须原样返回我们传进去的设备；GetDescW(5) 必须回读出
//   我们要求的字号和字体名。这两条成立 → 索引 3/5 没错位 →
//   13/14 是 DrawTextA/W 也就站得住。再顺手用 CALCRECT 量一次宽度，
//   索引 14 要真是错的，这里会在 SEH 里被抓住而不是把游戏带走。
static void CjkVerifyFont(void* dev, void* f, int heightPx)
{
    void** vt = *(void***)f;
    if (!vt || IsBadReadPtr(vt, 0x48)) return;

    struct D3DXFONT_DESCW_ {
        int   Height, Width, Weight, MipLevels, Italic;
        unsigned char CharSet, OutputPrecision, Quality, PitchAndFamily;
        WCHAR FaceName[32];
    };

    typedef long (__stdcall *GetDevice_t)(void*, void**);
    typedef long (__stdcall *GetDescW_t)(void*, void*);

    void* gd = NULL;
    long  hrG = -1;
    GetDevice_t gfn = (GetDevice_t)vt[D3DX_FONT_VTBL_GETDEVICE];
    if (gfn && InD3dx(gfn)) {
        __try { hrG = gfn(f, &gd); }
        __except (CjkTrap("ID3DXFont::GetDevice", GetExceptionInformation())) { hrG = -2; }
    }

    D3DXFONT_DESCW_ d;
    for (unsigned i = 0; i < sizeof(d); ++i) ((char*)&d)[i] = 0;   // 避免引入 memset 内建
    long hrD = -1;
    GetDescW_t dfn = (GetDescW_t)vt[D3DX_FONT_VTBL_GETDESCW];
    if (dfn && InD3dx(dfn)) {
        __try { hrD = dfn(f, &d); }
        __except (CjkTrap("ID3DXFont::GetDescW", GetExceptionInformation())) { hrD = -2; }
    }

    char face[64];
    face[0] = 0;
    WideCharToMultiByte(CP_ACP, 0, d.FaceName, -1, face, sizeof(face) - 1, NULL, NULL);

    int w = 0;
    static const WCHAR kProbe[] = { 0x4E2D, 0x6587, 0x5B57, L'A', L'B', L'C', 0 };
    CjkMeasureW(f, kProbe, &w);
    // 顺带看一眼视口尺寸——用来判断字号合不合理（比如 800x600 下 18px 正常，
    // 1920x1080 下 18px 就太小了）
    {
        struct VP { unsigned long x, y, w, h; float zmin, zmax; };
        void** dvt = *(void***)dev;
        typedef long (__stdcall *GetViewport_t)(void*, void*);
        // ★★ 48 才是 GetViewport —— 47 是 SetViewport。
        //    第五轮把它写成 47，等于拿全零的结构去 SetViewport，
        //    直接把设备的视口改成了 0x0（hr=0 表示 D3D 接受了这个值）。
        //    完整索引表见下方「D3D 虚表索引」一节（宏定义在文件更下方）。
        GetViewport_t vfn = dvt ? (GetViewport_t)dvt[48] : NULL;
        HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
        unsigned long b9 = (unsigned long)d3d9;
        unsigned long s9 = d3d9 ? PEImageSize(d3d9) : 0;
        unsigned long vf = (unsigned long)vfn;
        if (vfn && s9 && vf >= b9 && vf < b9 + s9) {
            VP vp;
            for (unsigned i = 0; i < sizeof(vp); ++i) ((char*)&vp)[i] = 0;
            long hrV = -1;
            __try { hrV = vfn(dev, &vp); }
            __except (CjkTrap("IDirect3DDevice9::GetViewport", GetExceptionInformation())) { hrV = -2; }
            LogPrintf("[cjk] viewport: hr=0x%08lX %lux%lu (z %.2f..%.2f)\r\n",
                      (unsigned long)hrV, vp.w, vp.h, (double)vp.zmin, (double)vp.zmax);
        }
    }

    LogPrintf("[cjk] verify: GetDevice hr=0x%08lX dev=%p match=%d\r\n",
              (unsigned long)hrG, gd, (gd == dev) ? 1 : 0);
    LogPrintf("[cjk] verify: GetDescW  hr=0x%08lX h=%d cs=%u face=\"%s\"\r\n",
              (unsigned long)hrD, d.Height, (unsigned)d.CharSet, face);
    LogPrintf("[cjk] verify: CALCRECT \"中文字ABC\" -> w=%d (h=%d)\r\n", w, heightPx);
    FlushFileBuffers(g_log);
}

static long CjkDrawLineW(void* font, const WCHAR* s, int x, int y)
{
    if (!font || !s) return -1;
    // ★★ 同一个坑第三次踩（2026-10-03 第四轮，直接崩在这）：
    //    void** vt = (void**)font;   ← font 是 ID3DXFont 对象，不是虚表
    //    那样 vt[14] 读到的是对象数据成员，被当函数指针 call 出去 →
    //    EIP = 垃圾值（实测 768FCA5E），EAX/EDX/EBX 全是对象里的字段。
    //    必须解一层。
    void** vt = *(void***)font;
    if (!vt || IsBadReadPtr(vt, 0x40)) return -1;

    DrawTextW_t dt = (DrawTextW_t)vt[D3DX_FONT_VTBL_DRAWTEXTW];
    if (!dt || IsBadReadPtr((const void*)dt, 1)) return -1;

    // 保险：虚表项必须落在 d3dx9_43.dll 映像内，否则绝不 call。
    // 这一条能在"万一偏移又错了"的场合把崩溃降级成"不显示中文"。
    if (!InD3dx(dt)) {
        g_cjkBadVtbl++;
        if (g_cjkBadVtbl <= 3)
            LogPrintf("[cjk] DrawTextW vtbl ptr %p outside d3dx9_43 (%p..%p) -> skip\r\n",
                      dt, (void*)g_d3dx,
                      (void*)((unsigned long)g_d3dx + PEImageSize(g_d3dx)));
        return -2;
    }

    RECT r;
    r.left   = x;
    r.top    = y;
    r.right  = x + 4096;      // 配合 DT_NOCLIP/SINGLELINE，右边界不参与裁剪
    r.bottom = y + 4096;

    // pSprite = NULL → D3DX 内部自建 sprite 并自己 Begin/End
    long hr = -3;
    __try {
        hr = dt(font, NULL, s, -1, &r,
                DT_SINGLELINE_ | DT_NOCLIP_ | DT_NOPREFIX_,
                0xFFFFFFFFul);  // D3DCOLOR_ARGB 不透明白
    } __except (CjkTrap("ID3DXFont::DrawTextW", GetExceptionInformation())) {
        hr = -4;
    }
    g_lastDrawHr = hr;
    return hr;
}

// 把一段 UTF-16 转成 GBK 塞进日志（控制符替换成 '.'，最多 40 字）
static void CjkWideToLog(const WCHAR* s, char* out, int cap)
{
    if (!out || cap <= 1) return;
    out[0] = 0;
    if (!s) return;

    int n = 0;
    while (n < CJKQ_CHARS && s[n]) ++n;   // 上界 256，数组本身就是这么大
    if (n > 40) n = 40;
    if (n <= 0) return;

    int got = WideCharToMultiByte(CP_ACP, 0, s, n, out, cap - 1, NULL, NULL);
    if (got <= 0) { out[0] = 0; return; }
    out[got] = 0;
    for (int i = 0; i < got; ++i)
        if ((unsigned char)out[i] < 0x20) out[i] = '.';
}

// ================================================================ 手动 CJK 渲染器
//
// ★★ 为什么放弃 ID3DXFont（第六轮实测定案）
//     [cjk] verify: CALCRECT "中文字ABC" -> w=0
//     [cjk #N] ... drawHr=0x00000000
//   ★ ID3DXFont::DrawText 返回的是**文本高度**，不是 HRESULT —— 返回 0 == 失败。
//     连纯测量的 DT_CALCRECT 都量出 0 宽，说明失败发生在 D3DX 内部的
//     ID3DXSprite::Begin（D3DXCreateFontA / GetDescW 全都成功，坏的只有
//     sprite 那条路）。再换参数重建成字体也救不了。
//
//   所以改成完全自己来，每个环节都在手里：
//     GDI 光栅化（CreateFontW + ExtTextOutW）→ 32 位 DIB → 纹理 → 自画四边形
//   不依赖 d3dx9_43 的任何东西。

// ★ atlas 当**字形缓存**用：每个文本项占一块独占区域（见 ManAtlasAlloc）。
//   容量要装得下一帧里所有文本项 —— 实测主菜单一帧 9 项、HUD 更多，
//   1024x256 按 shelf 打包约能放 90+ 项，够用。
//   ★ 注意：每次 LockRect/UnlockRect **只锁被分配的那一小块子矩形**，
//     所以 atlas 变大不会拖慢单次上传（之前从 256 降到 96 是误判，
//     那时的问题其实是"所有项都往 (0,0) 挤"，不是 atlas 太大）。
#define MAN_ATLAS_W   1024
// ★ 512（原 256）：每帧重渲染会让分配指针单调前进，给两帧的项留余量，
//   避免过早绕回覆盖仍在使用的槽位（绕回时调用方会 ManAtlasReset）。
#define MAN_ATLAS_H   512
// 每个槽四周留 1px 透明边：线性过滤采样到边界时会取到邻格，
// 留一圈黑边就不会被隔壁槽的内容污染。
#define MAN_SLOT_PAD  1
#define MAN_MAXSTR    240

#define D3DPT_TRIANGLELIST_      4
#define D3DFVF_XYZRHW_           0x004
#define D3DFVF_DIFFUSE_          0x040
#define D3DFVF_TEX1_             0x100
#define MAN_FVF                  (D3DFVF_XYZRHW_|D3DFVF_DIFFUSE_|D3DFVF_TEX1_)  // 0x144
#define D3DFMT_A8R8G8B8_         21
// ★★★ D3DPOOL 真实枚举：DEFAULT=0 MANAGED=1 SYSTEMMEM=2 SCRATCH=3
//   这里原先写成 3 = **D3DPOOL_SCRATCH** —— SCRATCH 池的纹理
//   “永远不能被设备使用”（只能当暂存介质），于是：
//     · LockRect/UnlockRect 一切正常（pitch=2560 完全正确）
//     · SetTexture 绑上去也返回成功
//     · **但采样到的是垃圾 ⇒ 中文整块不可见** ← 第 17 轮「文本空白」的真凶
//   改成 1（真正的 MANAGED）。
#define D3DPOOL_MANAGED_         1
#define D3DRS_ZENABLE_           7
#define D3DRS_ALPHABLENDENABLE_  27
#define D3DRS_SRCBLEND_          19
#define D3DRS_DESTBLEND_         20
#define D3DRS_ALPHATESTENABLE_   15   // ★ 曾经误写成 24（那是 D3DRS_ALPHAREF）
#define D3DRS_CULLMODE_          22
#define D3DRS_LIGHTING_          137
#define D3DRS_FOGENABLE_         28
#define D3DBLEND_SRCALPHA_       5
#define D3DBLEND_INVSRCALPHA_    6
#define D3DCULL_NONE_            1
#define D3DTSS_COLOROP_          1
#define D3DTSS_COLORARG1_        2
#define D3DTSS_COLORARG2_        3
#define D3DTSS_ALPHAOP_          4
#define D3DTSS_ALPHAARG1_        5
#define D3DTSS_ALPHAARG2_        6
#define D3DTOP_MODULATE_         4
#define D3DTOP_SELECTARG2_       3   // 直接输出 arg2（用来画不依赖纹理的纯色块）
// ★★ D3DTA 真实枚举：DIFFUSE=0 CURRENT=1 TEXTURE=2 TFACTOR=3
//   原先写 1 其实是 D3DTA_CURRENT（第 0 级恰好等价于 TEXTURE，所以侥幸没炸，
//   但那只是巧合 —— 一旦有多级纹理就错了）。改成真值 2。
#define D3DTA_DIFFUSE_           0x00000000
#define D3DTA_CURRENT_           0x00000001
#define D3DTA_TEXTURE_           0x00000002
#define D3DSAMP_MINFILTER_       6
#define D3DSAMP_MAGFILTER_       5
#define D3DSAMP_MIPFILTER_       7
#define D3DTEXF_NONE_            0
#define D3DTEXF_LINEAR_          2
#define D3DTS_VIEW_              2u
#define D3DTS_PROJECTION_        3u
#define D3DTS_WORLD_             256u

// ================================================================ D3D 虚表索引
//
// ★★★ 本项目被这个坑埋了两次雷，务必按下面这张表走，别凭记忆写：
//   · 第五轮 DrawTextW 写成 9（GetGlyphData）→ 崩
//   · 这一轮 SetVertexShader 写成 92、LockRect 写成 8 → 同样会崩
//
// 能从二进制实测的一律标「实测」；标「推导」的是按接口继承层次数出来的，
// 无法直接反推（引擎走 D3DX 建纹理），已在 ManEnsure() 里加 GetLevelDesc
// 回读校验 + SEH 兜底，索引再错也只是「不显示中文」而不是崩游戏。
//
//   IDirect3DDevice9
//     16 Reset / 17 Present / 41 BeginScene / 42 EndScene
//     45 GetTransform / 48 GetViewport / 18 GetBackBuffer
//     57 SetRenderState   ← 实测：push 0Eh/13h/1Bh + 值，75 处调用
//     65 SetTexture       ← 实测：push 纹理指针 + 0
//     67 SetTextureStageState ← 实测：push 0/1/2/4/6 + 值
//     69 SetSamplerState  ← 实测：push 0/2/5/6 + 值
//     83 DrawPrimitiveUP  ← 实测：push 2, 6(=D3DPT_TRIANGLELIST/2 tri)
//     89 SetFVF           ← 实测：push 144h（XYZRHW|DIFFUSE|TEX1）
//    100 SetStreamSource  ← 实测：push 1Ch（= 顶点结构 28 字节步长）
//    107 SetVertexShader  ← 实测：push 0（= 解绑 VS）
//     59  CreateStateBlock ← 由 57 顺推，与 65/67/69 三点互验（第 13 轮实测可用）
//
//   ★ 纹理接口一律**运行时探测**，不要写死（历史三个错值全部作废）：
//       CreateTexture 曾猜 22 → 实机返回 0xDEADBEEF（改用 D3DXCreateTexture 导出函数）
//       LockRect 曾猜 8/9、14/15 → **实测真值 19/20**（CjkProbeTex 扫出来）
//     IDirect3DTexture9 继承 IDirect3DResource9（8 个方法）而不是直接继承 IUnknown，
//     凭印象数必然错位 —— 这就是为什么必须探测。
//
// 没有任何一个 SetPixelShader 调用点 ⇒ 游戏全程不绑 PS ⇒ 不需要解绑 PS。

#define VTI_RESET               16
#define VTI_PRESENT             17
#define VTI_GETBACKBUFFER       18
#define VTI_GETTRANSFORM        45
#define VTI_GETVIEWPORT         48
#define VTI_SETRENDERSTATE      57
#define VTI_SETTEXTURE          65
#define VTI_SETTEXTURESTAGESTATE 67
#define VTI_SETSAMPLERSTATE     69
#define VTI_DRAWPRIMITIVEUP     83
#define VTI_SETFVF              89
#define VTI_SETSTREAMSOURCE     100
#define VTI_SETVERTEXSHADER     107


// ★ 纹理接口的槽位一律**运行时探测**（见 CjkProbeTex），不要硬编码：
//   VTI_TEX_LOCKRECT / VTI_TEX_UNLOCKRECT / VTI_CREATETEXTURE 都已废弃。
//   原因：引擎走 D3DX 内部建纹理，exe 里没有直接调用点可反推，
//   而"按接口继承层次数槽位"已连续三次出错。
//   （历史错误值留档：CreateTexture=22 返回 0xDEADBEEF；LockRect=8/9 会调到
//     GetPriority/PreLoad；DrawTextW=9 会调到 GetGlyphData。）
#define VTI_TEX_GETLEVELDESC    11

// IDirect3DSurface9::GetDesc
#define VTI_SURF_GETDESC        8

// ★ D3DXCreateTexture 是 8 参（d3dx9tex.h），**没有** pSharedHandle。
//   之前多声明了一个 pDataRect 并且真的多传了一个 NULL —— __stdcall 的
//   callee 只弹 32 字节，会在栈上留 4 字节残留（UB）。已对齐成 8 参。
typedef long (__stdcall *D3DXCreateTexture_t)(void* device, unsigned width, unsigned height,
                                              unsigned mipLevels, unsigned usage, int format,
                                              int pool, void** ppTexture);
typedef long (__stdcall *TexLockRect_t)(void*, unsigned, void*, const void*, unsigned long);
typedef long (__stdcall *TexUnlockRect_t)(void*, unsigned);
typedef long (__stdcall *TexGetLevelDesc_t)(void*, unsigned, void*);
typedef long (__stdcall *SetRenderState_t)(void*, int, unsigned long);
typedef long (__stdcall *SetTSS_t)(void*, unsigned, int, unsigned long);
typedef long (__stdcall *SetSamplerState_t)(void*, unsigned, int, unsigned long);
typedef long (__stdcall *SetTexture_t)(void*, unsigned, void*);
typedef long (__stdcall *SetFVF_t)(void*, unsigned long);
typedef long (__stdcall *SetVS_t)(void*, void*);
typedef long (__stdcall *DrawUP_t)(void*, int, unsigned, const void*, unsigned);
typedef long (__stdcall *GetViewport_t)(void*, void*);
typedef long (__stdcall *GetTransform_t)(void*, unsigned long, void*);
typedef long (__stdcall *GetBackBuffer_t)(void*, unsigned, unsigned, int, void**);
typedef long (__stdcall *SurfGetDesc_t)(void*, void*);

struct ManLockedRect { int Pitch; void* pBits; };
struct ManVert { float x, y, z, rhw; unsigned long d; float u, v; };
struct ManVP   { unsigned long x, y, w, h; float zmin, zmax; };
struct ManSurfDesc { int fmt, type, usage, pool; unsigned long msType, msQual;
                     unsigned w, h; };
// ★ GetLevelDesc 用一块裸 64 字节缓冲接，按 DWORD 直读 w/h/pitch ——
//   不定义 D3DDESC9 结构体，因为我对自己猜的结构偏移没有把握
//   （第 9 轮实测读回来全是 0）。缓冲必须给够，d3d9 会按 D3DDESC9 全量写。

static D3DXCreateTexture_t g_pCreateTex = NULL;

// ---- 自动截帧（诊断）----
static HDC      g_dibdc    = NULL;
static HBITMAP  g_dibbm    = NULL;
static void*    g_dibbits  = NULL;
// ---- 字体缓存（★ 性能关键）----
// 实测同一帧里就有两种字号（10 / 18）且**交替出现**。旧代码在字号变化时
// `DeleteObject + CreateFontW`，等价于「每画一行就重建一次字体」，
// 而 CreateFontW 是 GDI 里最慢的调用之一 —— 实测直接把帧率打到个位数。
// 而且 DeleteObject 一个仍被 SelectObject 的字体是**失败**的（句柄泄漏）。
// 现在按字号缓存最多 6 个 HFONT，切换只做一次 SelectObject。
#define MAN_FONT_CACHE 6
static HFONT g_fonts[MAN_FONT_CACHE];
static int   g_fontPxs[MAN_FONT_CACHE];
static int   g_fontUsed[MAN_FONT_CACHE];
static int   g_fontN     = 0;
static int   g_fontTick  = 0;
static int   g_fontSelPx = -1;    // 当前已选入 DIB DC 的字号（-1 = 无）
static void*    g_tex      = NULL;     // IDirect3DTexture9*
static void*    g_texDev   = NULL;
// ---- 顶点数据环形缓冲 ----
// ★★★ 第 19 轮：原来所有 draw 共用同一个全局数组 `g_verts`。
//   如果 D3D9 转译层/驱动在 flush 时才去读顶点数据（而不是在 call 时
//   立刻拷贝），那么**所有四边形都会用最后一份坐标** ⇒ 屏幕上只剩一行字
//   （几个四边形成了一张叠在一起），而且随绘制顺序变化而闪烁。
//   实测症状吻合（主菜单 6 项只看到最后画的「新游戏」被"合并"成一行）。
//   → 每次绘制从环形缓冲取一块独占的顶点数据，至少保证同一帧内不互相踩。
#define MAN_VRING 128
static ManVert  g_vring[MAN_VRING][12];   // 12 = 两份四边形（实验用）
static unsigned g_vringIdx = 0;

// 取一块干净的 6 顶点（z/rhw/颜色预置），调用方只填 x/y/u/v
static ManVert* ManVerts(void)
{
    ManVert* v = g_vring[g_vringIdx % MAN_VRING];
    ++g_vringIdx;
    for (int i = 0; i < 6; ++i) { v[i].z = 0.0f; v[i].rhw = 1.0f; v[i].d = 0xFFFFFFFFul; }
    return v;
}

static int      g_manFail  = 0;        // 设备相关失败次数（可重试几次：设备可能还没就绪）
static int      g_manFatal = 0;        // 永久性失败（DIB/DC/字体建不出来，重试也没用）
static unsigned long g_manDraws  = 0;
static int           g_traceLeft = 4;   // 前 4 次绘制逐步留痕（倒计数，绝不刷屏）
static long          g_manLastHr = -999;

// ★ 纹理对象的虚表在 **d3d9.dll**，不在 d3dx9_43.dll。
//   实测：`D3DXCreateTexture` 只是转发给设备的 CreateTexture，吐出来的
//   仍是原厂 IDirect3DTexture9（日志：vtbl[11]=6F91D260，d3d9 基址 6F890000）。
//   所以纹理相关一律用 InD3d9 判定；D3DX 的**导出函数**才用 InD3dx。
// 取"引擎实际在用的设备"。
// ★ 实测：引擎 Present 用的是 [screen+0xE28]，与我们 IAT 捕获的 g_device 未必同一个
//   （我们 hook 了 g_device 的 Present，但 present 计数恒 0）。
//   纹理与绘制都必须落在**引擎真正呈现的那个设备**上，否则画了也看不到。
static void* CjkActiveDevice(void)
{
    if (g_exe && !IsBadReadPtr(g_exe, 4)) {
        void** pScr = (void**)((BYTE*)g_exe + RVA_GlobalScreen);
        if (!IsBadReadPtr(pScr, 4) && *pScr) {
            void** pDev = (void**)((char*)*pScr + OFF_EngineDev);
            if (!IsBadReadPtr(pDev, 4) && *pDev && LooksLikeDevice(*pDev))
                return *pDev;
        }
    }
    return g_device;
}

static int InD3d9(const void* fn)
{
    if (!fn) return 0;
    HMODULE m = GetModuleHandleA("d3d9.dll");
    if (!m) return 0;
    unsigned long base = (unsigned long)m;
    unsigned long size = PEImageSize(m);
    unsigned long v    = (unsigned long)fn;
    return (size && v >= base && v < base + size) ? 1 : 0;
}

static void ZeroBuf(void* p, unsigned n)
{
    char* c = (char*)p;
    for (unsigned i = 0; i < n; ++i) c[i] = 0;
}

// ================================================================ 安全间接调用
//
// ★★ 第 12 轮崩溃教训（EIP 落在栈地址、WRITE 违例、ESP 失衡）：
//   `__stdcall` 的 callee 用 `ret N` 自己清栈。如果我们按错误的参数个数去调
//   一个槽位（比如把 SetStreamSource 当成 CreateStateBlock，只压 3 个参数，
//   而它 `ret 16`），ESP 就会偏 4 字节 → 返回地址错乱 → 跳到栈上执行。
//   `__try/__except` **抓不住这种错误**（不是异常，是栈被慢慢啃掉）。
//
// 对策：调用前后由我们**自己显式恢复 ESP**，完全不信任 callee 的 ret N。
//   即使参数个数猜错、即使 callee 多弹了几个字节，ESP 也会被拉回来。
extern "C" __declspec(naked) long __cdecl CjkSafeCall3(void* fn, void* a1, void* a2, void* a3)
{
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        // 4 个 push = 16 字节；再加返回地址 4 字节 ⇒ 参数从 [esp+20] 起
        mov  eax, [esp+20]        // fn
        mov  ebx, [esp+24]        // a1
        mov  ecx, [esp+28]        // a2
        mov  edx, [esp+32]        // a3
        push edx                  // 右到左压栈
        push ecx
        push ebx
        mov  ebp, esp             // 记住压完参数后的 ESP
        test eax, eax
        jz   short sc_skip
        call eax
    sc_skip:
        mov  esp, ebp             // ★ 无条件恢复 ESP，无视 callee 的 ret N
        add  esp, 12              // 丢弃我们压的 3 个参数
        pop  ebp
        pop  edi
        pop  esi
        pop  ebx
        ret
    }
}

// 5 参版本（纹理 LockRect 探测用：tex, Level, pLockedRect, pRect, Flags）
extern "C" __declspec(naked) long __cdecl CjkSafeCall5(void* fn, void* a1, void* a2,
                                                      void* a3, void* a4, void* a5)
{
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov  eax, [esp+20]        // fn
        mov  ebx, [esp+24]        // a1
        mov  ecx, [esp+28]        // a2
        mov  edx, [esp+32]        // a3
        mov  esi, [esp+36]        // a4
        mov  edi, [esp+40]        // a5
        push edi
        push esi
        push edx
        push ecx
        push ebx
        mov  ebp, esp
        test eax, eax
        jz   short sc5_skip
        call eax
    sc5_skip:
        mov  esp, ebp
        add  esp, 20
        pop  ebp
        pop  edi
        pop  esi
        pop  ebx
        ret
    }
}

// ================================================================ 渲染状态块
//
// ★★ 第 11 轮实机教训：改了 15 个渲染状态一个都不还 ⇒ UI 彻底损坏、黑白屏，
//   而 manHr=0（绘制本身是成功的）。hook 插在引擎绘制循环中间，
//   引擎后续绘制全部继承我们的设置：8 个 SetRenderState + 6 个 TSS +
//   3 个 SamplerState + SetFVF + SetVertexShader + 纹理绑定。
//
// 解法：`IDirect3DStateBlock9` 的 Capture/Apply 整包保存恢复 ——
//   天然覆盖 FVF / 顶点着色器 / 纹理绑定 / 各级渲染状态，不可能漏项。
//
// ★★ 槽位来源（不再盲扫）：从已实测锚点顺推，三点互验
//       57 SetRenderState  ← 实测（75 处调用）
//       58 GetRenderState   59 CreateStateBlock ← 本函数要的
//       60 BeginStateBlock  61 EndStateBlock
//       62 SetClipStatus    63 GetClipStatus
//       64 GetTexture       65 SetTexture            ← 65 实测
//       66 GetTextureStageState  67 SetTextureStageState ← 67 实测
//       68 GetSamplerState       69 SetSamplerState      ← 69 实测
//       ... 83 DrawPrimitiveUP ← 实测 ... 89 SetFVF ← 实测
//   整条链与三个独立实测点完全吻合。
//
// ★★★ 任何槽位调用都必须走 CjkSafeCall3（见文件上方）。
//   第 12 轮实机崩溃（EIP 落在栈地址、WRITE 违例）根因：
//   `__stdcall` 的 callee 用 `ret N` 自清栈，参数个数猜错 ⇒ ESP 偏移 ⇒
//   返回地址被踩 ⇒ 跳到栈上执行。`__try/__except` 抓不住这种错误。
// ★★★ 第 13 轮修正：CreateStateBlock **立即捕获**当前状态，
//   不需要（也不能）再调 `Capture()` —— `Capture` 只用于 `BeginStateBlock`
//   录制的块。多调一次 Capture 直接崩在 d3d9 内部
//   （实机 EIP=6F90A200 fault=0 WRITE，RVA 0x7A200 落在 d3d9 内部函数中段）。
//
//   正确生命周期（D3D9 教科书式，逐次创建/还原/释放）：
//     sb = CreateStateBlock(D3DSBT_ALL, &sb);   ← 此刻捕获引擎状态
//     ... 改状态 + 绘制 ...
//     sb->Apply();      ← 整包还原
//     sb->Release();
#define VTI_CREATESTATEBLOCK    59
#define VTI_SB_APPLY            4     // IDirect3DStateBlock9: 0 QI 1 AddRef 2 Release 3 Capture 4 Apply
// 诊断用"读取"槽位（都是推导值，调用一律走 CjkSafeCall3）
#define VTI_GETRENDERSTATE      58
#define VTI_GETFVF              90
#define VTI_GETTEXTURE          64    // D3D9 头文件里 Get* 在前 ⇒ 64/65 一对
#define VTI_GETTEXTURESTAGESTATE 66
#define VTI_GETSAMPLERSTATE     68

static int g_sbState = 0;             // 0=未试 1=可用 -1=不可用（只报一次）
static unsigned long g_sbCreateOk   = 0;
static unsigned long g_sbCreateFail = 0;
static unsigned long g_sbApply      = 0;   // 还原次数（诊断用）
static long          g_sbApplyHr    = -999;
static int           g_gettersFail  = 0;   // 0 未判 / -1 连续失败(静默)

// 创建状态块（同时捕获当前状态）。失败返回 NULL ⇒ 调用方放弃本行绘制。
static void* CjkStateBlockCreate(void* dev)
{
    if (!dev) return NULL;
    void** dvt = *(void***)dev;
    if (!dvt || IsBadReadPtr(dvt, 0x1B0)) return NULL;

    void* f = dvt[VTI_CREATESTATEBLOCK];
    if (!f || !InD3d9(f)) {
        if (g_sbState == 0) {
            g_sbState = -1;
            LogPrintf("[man] CreateStateBlock slot %d unavailable -> CJK draw disabled "
                      "(refusing to touch state without restore)\r\n", VTI_CREATESTATEBLOCK);
            FlushFileBuffers(g_log);
        }
        return NULL;
    }
    if (g_sbState == -1) return NULL;

    void* sb = NULL;
    long hr = CjkSafeCall3(f, dev, (void*)1 /*D3DSBT_ALL*/, &sb);
    if (hr != 0 || !sb || IsBadReadPtr(sb, 8)) {
        if (g_sbCreateFail < 3) {
            LogPrintf("[man] CreateStateBlock hr=0x%08lX sb=%p\r\n", (unsigned long)hr, sb);
            FlushFileBuffers(g_log);
        }
        g_sbCreateFail++;
        if (g_sbCreateFail >= 8) g_sbState = -1;
        return NULL;
    }
    void** vt = *(void***)sb;
    if (IsBadReadPtr(vt, 0x20) || !InD3d9(vt[VTI_SB_APPLY]) || !InD3d9(vt[2])) {
        if (g_sbCreateFail < 3) {
            LogPrintf("[man] CreateStateBlock -> not a StateBlock %p (vt2=%p vt4=%p)\r\n",
                      sb, vt ? vt[2] : NULL, vt ? vt[VTI_SB_APPLY] : NULL);
            FlushFileBuffers(g_log);
        }
        g_sbCreateFail++;
        if (g_sbCreateFail >= 8) g_sbState = -1;
        return NULL;
    }

    if (g_sbCreateOk == 0) {
        LogPrintf("[man] state block ok sb=%p (dev vtbl[%d]) -> save/restore active\r\n",
                  sb, VTI_CREATESTATEBLOCK);
        FlushFileBuffers(g_log);
    }
    g_sbCreateOk++;
    g_sbState = 1;
    return sb;
}

// Apply（整包还原）→ Release（释放）。两者都走安全调用。
static long CjkStateBlockApplyRelease(void* sb)
{
    if (!sb) return -1;
    void** vt = *(void***)sb;
    if (IsBadReadPtr(vt, 0x20)) return -1;
    long hr = -1;
    void* ap = vt[VTI_SB_APPLY];
    if (ap && InD3d9(ap)) hr = CjkSafeCall3(ap, sb, NULL, NULL);
    void* rel = vt[2];
    if (rel && InD3d9(rel)) CjkSafeCall3(rel, sb, NULL, NULL);
    g_sbApply++;
    g_sbApplyHr = hr;
    return hr;
}

// ================================================================ 纹理接口自发现
//
// ★★★ 本项目被"虚表索引"埋了三次：ID3DXFont 的 DrawTextW(9→14)、
//   LockRect(8/9→14/15)、IDirect3DDevice9::CreateTexture(22，返回 0xDEADBEEF)。
//   根因：这些接口的槽位**无法从引擎二进制反推**（引擎走 D3DX 内部建纹理，
//   exe 里根本没有直接调用点），而"按接口继承层次数出来"是不可靠的直觉。
//
// 结论：凡是猜不出、也没法实测的槽位，一律改成**运行时探测**。
// LockRect 的判据非常好认（唯一会返回 hr==0 + 指向可写像素的槽位）：
//   hr == 0 && pBits != NULL && (Pitch % 4 == 0) && |Pitch| >= 宽度*4/4
// UnlockRect 就在它后面第 2 格（D3D9 里二者相邻，中间隔 AddDirtyRect）。
// 探测全程 __try 包着 —— 猜错最坏是这次探测失败，不崩游戏。
//
// 探测结果缓存：g_texLockIdx 记住槽位号，纹理没换就复用，不重复试。
static int  g_texLockIdx   = -1;
static int  g_texUnlockIdx = -1;
static int  g_texProbeDone = 0;
static void* g_texProbeDev = NULL;   // 探测结果所属设备（换设备要重探）

// 设备丢失/重置时统一作废所有绑定到旧设备的资源。
// 供 Reset 钩子调用 —— 那些资源在 Reset 后就是野指针，不清会崩（第 10 轮）。
static void CjkDropDeviceResources(void)
{
    g_tex         = NULL;
    g_texDev      = NULL;
    g_texProbeDev = NULL;   // 强制重探 LockRect/UnlockRect 槽位
    // 状态块现在是「每次绘制临时创建 + Apply + Release」，无需在这里清
}

static void CjkProbeTex(void* tex, TexLockRect_t* outLock, TexUnlockRect_t* outUnlock)
{
    *outLock = NULL; *outUnlock = NULL;
    if (!tex) return;

    if (g_texProbeDone && g_texLockIdx >= 0) {
        void** tvt = *(void***)tex;
        if (!IsBadReadPtr(tvt, 0x40)) {
            *outLock   = (TexLockRect_t)tvt[g_texLockIdx];
            *outUnlock = (TexUnlockRect_t)tvt[g_texUnlockIdx];
            if (InD3d9(*outLock) && InD3d9(*outUnlock)) return;
        }
    }

    void** tvt = *(void***)tex;
    if (IsBadReadPtr(tvt, 0x80)) return;

    // IDirect3DTexture9 继承 IDirect3DResource9 ⇒ 有效槽位在 0..~19
    for (int i = 8; i <= 22; ++i) {
        TexLockRect_t f = (TexLockRect_t)tvt[i];
        if (!InD3d9(f)) continue;

        // ★ 用 256 字节大缓冲承接：若这个槽其实是 GetLevelDesc，
        //   它会往里写 ~124 字节的 D3DDESC9 —— 给 8 字节的结构体会直接砸栈。
        unsigned char pad[256];
        ZeroBuf(pad, sizeof(pad));
        RECT r;
        r.left = 0; r.top = 0; r.right = 16; r.bottom = 16;

        // ★★ 走安全调用：参数个数猜错时由我们自己恢复 ESP
        long hr = CjkSafeCall5((void*)f, tex, NULL, pad, &r, NULL);
        ManLockedRect lr;
        lr.Pitch = *(int*)(pad + 0);
        lr.pBits = *(void**)(pad + 4);

        if (hr != 0 || !lr.pBits) continue;
        if (lr.Pitch <= 0 || lr.Pitch > 65536) continue;
        if ((lr.Pitch % 4) != 0) continue;
        // ★ 锁的是 16x16 区域，pitch 至少要装得下 16 像素 ×4 字节。
        //   这条能排除掉那些"也返回 hr==0 + 垃圾 pBits"的无关槽位。
        if (lr.Pitch < 64) continue;

        // 找它后面的 UnlockRect（D3D9 里 LockRect/UnlockRect 相邻）
        TexUnlockRect_t u = NULL;
        for (int k = i + 1; k <= i + 3 && k <= 22; ++k) {
            TexUnlockRect_t g = (TexUnlockRect_t)tvt[k];
            if (!InD3d9(g)) continue;
            long h2 = CjkSafeCall5((void*)g, tex, NULL, NULL, NULL, NULL);
            if (h2 == 0) { u = g; g_texUnlockIdx = k; break; }
        }
        if (!u) continue;   // 只锁不Unlock = 大概率猜错了，不采用

        g_texLockIdx   = i;
        g_texUnlockIdx = g_texUnlockIdx;
        g_texProbeDone = 1;
        *outLock   = f;
        *outUnlock = u;

        LogPrintf("[man] tex vtable probe: LockRect=idx%d UnlockRect=idx%d  pitch=%d OK\r\n",
                  i, g_texUnlockIdx, lr.Pitch);
        FlushFileBuffers(g_log);
        return;
    }

    if (!g_texProbeDone) {
        g_texProbeDone = 1;   // 只探测一次，失败就别每帧刷日志
        LogPrintf("[man] tex vtable probe FAILED (scanned idx 8..22)\r\n");
        FlushFileBuffers(g_log);
    }
}

// ================================================================ 字体配置（update\chs.ini）
//
// 用户需求：提供一个 ini，可配置"加载本目录下的 ttf"，默认 SourceHanSansHWSC-VF.ttf。
//
//   [font]
//   file=SourceHanSansHWSC-VF.ttf   ; 相对 update\ 目录的字体文件；留空/不存在 => 用 face/file 回退
//   face=                           ; 留空 = 自动从 ttf 的 'name' 表读取真实族名
//   quality=4                       ; 0=DEFAULT 1=DRAFT 2=PROOF 4=ANTIALIASED 5=CLEARTYPE
//
// 实现要点：
//   · ini 用 GetPrivateProfileStringA 读（kernel32，无需 CRT）
//   · 字体用 AddFontResourceExW(path, FR_PRIVATE, 0) **私有加载** —— 不污染系统字体表
//   · 族名自动解析 ttf 的 'name' 表（nameID=1），先英文(0x409)再中文(0x804)；
//     每个候选都用 GetTextFaceW 回读校验，选不上就换下一个，最终回退 SimSun
static char  g_cfgFontFile[MAX_PATH] = "SourceHanSansHWSC-VF.ttf";
static char  g_cfgFontFace[128]      = "";
static double g_cfgScaleMul          = 0.0;   // 0 = 按字体自身度量自动计算
// ---- 布局微调（[layout] 段）----
//   xoff/yoff：整体把字往右/下挪多少像素（负数=左/上）。任何字号都生效。
//   mode：0=自动 1=屏幕坐标(不加矩阵) 2=矩阵(WVP)
static int    g_cfgXoff              = 0;
static int    g_cfgYoff              = 0;
static int    g_cfgMapMode           = 0;
static int   g_cfgQuality            = 4;              // ANTIALIASED_QUALITY
static int   g_cfgLoaded             = 0;
static int   g_cfgTtfOk              = 0;
static int   g_cfgFaceOk             = 0;
static WCHAR g_faceW[LF_FACESIZE]    = L"SimSun";       // 实际传给 CreateFontW 的族名
static char  g_faceA[LF_FACESIZE]    = "SimSun";        // 日志用（GBK）
static char  g_faceFallback[64]      = "";              // 记录为什么回退

static unsigned long ttfBE32(const unsigned char* p)
{ return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }
static unsigned ttfBE16(const unsigned char* p)
{ return ((unsigned)p[0] << 8) | p[1]; }

// 解析 ttf 的 'name' 表取族名（nameID=1）。只读头部与 name 表，不动 35MB 正文。
static unsigned char g_ttfBuf[65536];
static int TtfReadFamily(const WCHAR* path, WCHAR* out, int cap, unsigned lang)
{
    out[0] = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    unsigned char hdr[12]; DWORD got = 0;
    if (!ReadFile(h, hdr, 12, &got, NULL) || got != 12) { CloseHandle(h); return 0; }
    unsigned num = ttfBE16(hdr + 4);
    if (num > 64) num = 64;

    unsigned long nameOff = 0, nameLen = 0;
    unsigned char rec[16];
    for (unsigned i = 0; i < num; ++i) {
        if (!ReadFile(h, rec, 16, &got, NULL) || got != 16) break;
        if (rec[0] == 'n' && rec[1] == 'a' && rec[2] == 'm' && rec[3] == 'e') {
            nameOff = ttfBE32(rec + 8);
            nameLen = ttfBE32(rec + 12);
            break;
        }
    }
    if (!nameOff || !nameLen) { CloseHandle(h); return 0; }
    if (nameLen > sizeof(g_ttfBuf)) nameLen = sizeof(g_ttfBuf);
    if (SetFilePointer(h, (LONG)nameOff, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        CloseHandle(h); return 0;
    }
    if (!ReadFile(h, g_ttfBuf, nameLen, &got, NULL) || got < 6) { CloseHandle(h); return 0; }
    CloseHandle(h);

    unsigned count = ttfBE16(g_ttfBuf + 2);
    unsigned so    = ttfBE16(g_ttfBuf + 4);
    if (count > 256) count = 256;

    // lang = 0 => 试英文(0x409) 成功即返回；否则只认给定语言
    for (int pass = 0; pass < (lang ? 1 : 2); ++pass) {
        unsigned want = lang ? lang : (pass == 0 ? 0x0409u : 0x0804u);
        for (unsigned i = 0; i < count; ++i) {
            const unsigned char* r = g_ttfBuf + 6 + i * 12;
            if (r + 12 > g_ttfBuf + got) break;
            unsigned pid = ttfBE16(r), eid = ttfBE16(r + 2), lid = ttfBE16(r + 4);
            unsigned nid = ttfBE16(r + 6), len = ttfBE16(r + 8), o = ttfBE16(r + 10);
            if (nid != 1 || pid != 3 || eid != 1 || lid != want) continue;
            if ((unsigned long)so + o + len > got || len < 2) continue;
            const unsigned char* sb = g_ttfBuf + so + o;
            int k = 0;
            for (unsigned j = 0; j + 1 < len && k < cap - 1; j += 2) {
                unsigned ch = ((unsigned)sb[j] << 8) | sb[j + 1];
                if (!ch) break;
                out[k++] = (WCHAR)ch;
            }
            out[k] = 0;
            if (k > 0) return 1;
        }
    }
    return 0;
}

// 读 ttf 的 head.unitsPerEm 与 OS/2.usWinAscent/usWinDescent。
// ★★ 为什么需要它（用户反馈"字比 SimSun 小很多"的根因）：
//   GDI 的 lfHeight 是**字符单元格高度**，字体把自己声明得越高，em 就被压得越小。
//     SimSun      : upem=256, winAsc+winDesc=256  ⇒ 单元格 = 1.000 em ⇒ lfHeight 全部给到 em
//     SourceHanSans: upem=1000, winAsc+winDesc=1448 ⇒ 单元格 = 1.448 em ⇒ em 只有 lfHeight/1.448
//   两者墨高比 = 0.69 —— 这就是"变小"的全部原因。补偿系数 = cell/upem。
static double g_fontScale = 1.0;
static int    g_fontUpem = 0, g_fontWinA = 0, g_fontWinD = 0;

static int TtfReadVert(const WCHAR* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    unsigned char hdr[12]; DWORD got = 0;
    if (!ReadFile(h, hdr, 12, &got, NULL) || got != 12) { CloseHandle(h); return 0; }
    unsigned num = ttfBE16(hdr + 4);
    if (num > 64) num = 64;

    unsigned long headOff = 0, os2Off = 0;
    unsigned char rec[16];
    for (unsigned i = 0; i < num; ++i) {
        if (!ReadFile(h, rec, 16, &got, NULL) || got != 16) break;
        if (rec[0] == 'h' && rec[1] == 'e' && rec[2] == 'a' && rec[3] == 'd') headOff = ttfBE32(rec + 8);
        if (rec[0] == 'O' && rec[1] == 'S' && rec[2] == '/' && rec[3] == '2') os2Off  = ttfBE32(rec + 8);
    }
    int ok = 0;
    if (headOff) {
        if (SetFilePointer(h, (LONG)headOff + 18, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
            ReadFile(h, rec, 2, &got, NULL) && got == 2) {
            g_fontUpem = ttfBE16(rec);
            ok = 1;
        }
    }
    if (os2Off) {
        if (SetFilePointer(h, (LONG)os2Off + 74, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
            ReadFile(h, rec, 4, &got, NULL) && got == 4) {
            g_fontWinA = ttfBE16(rec);
            g_fontWinD = ttfBE16(rec + 2);
        }
    }
    CloseHandle(h);
    if (!ok || g_fontUpem <= 0) return 0;
    return 1;
}

// 试着用 face 建字体，并用 GetTextFaceW 回读确认真的选中了它（而不是被 GDI 换掉）。
static int CjkFontFaceUsable(const WCHAR* face)
{
    if (!face || !face[0]) return 0;
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) return 0;
    HFONT f = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, GB2312_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          g_cfgQuality, DEFAULT_PITCH, face);
    int ok = 0;
    if (f) {
        HGDIOBJ old = SelectObject(dc, f);
        WCHAR got[LF_FACESIZE]; got[0] = 0;
        if (GetTextFaceW(dc, LF_FACESIZE, got) > 0 && got[0]) {
            int same = 1;
            for (int i = 0; ; ++i) {
                WCHAR a = face[i], b = got[i];
                if (a >= L'A' && a <= L'Z') a = (WCHAR)(a + 32);
                if (b >= L'A' && b <= L'Z') b = (WCHAR)(b + 32);
                if (a != b) { same = 0; break; }
                if (!a) break;
            }
            ok = same;
        }
        SelectObject(dc, old);
        DeleteObject(f);
    }
    DeleteDC(dc);
    return ok;
}

static void CjkWToA(const WCHAR* w, char* a, int cap)
{
    int i = 0;
    for (; w[i] && i < cap - 1; ++i) a[i] = (w[i] < 0x100) ? (char)w[i] : '?';
    a[i] = 0;
}

// 一次性读 ini + 加载字体。失败路径全部留日志，绝不静默。
static void CjkLoadFontConfig(void)
{
    if (g_cfgLoaded) return;
    g_cfgLoaded = 1;

    char ini[MAX_PATH];
    CjkSidePath("chs.ini", ini, sizeof(ini));
    int iniExists = (GetFileAttributesA(ini) != INVALID_FILE_ATTRIBUTES);
    if (iniExists) {
        GetPrivateProfileStringA("font", "file",  "SourceHanSansHWSC-VF.ttf",
                                 g_cfgFontFile, sizeof(g_cfgFontFile), ini);
        GetPrivateProfileStringA("font", "face",  "", g_cfgFontFace, sizeof(g_cfgFontFace), ini);
        g_cfgXoff    = (int)GetPrivateProfileIntA("layout", "xoff", 0, ini);
        g_cfgYoff    = (int)GetPrivateProfileIntA("layout", "yoff", 0, ini);
        g_cfgMapMode = (int)GetPrivateProfileIntA("layout", "mode", 0, ini);
        if (g_cfgXoff < -4096) g_cfgXoff = -4096;
        if (g_cfgXoff >  4096) g_cfgXoff =  4096;
        if (g_cfgYoff < -4096) g_cfgYoff = -4096;
        if (g_cfgYoff >  4096) g_cfgYoff =  4096;
        if (g_cfgMapMode < 0 || g_cfgMapMode > 2) g_cfgMapMode = 0;
        {
            char sb[32];
            GetPrivateProfileStringA("font", "scale", "0", sb, sizeof(sb), ini);
            g_cfgScaleMul = 0.0;
            for (int i = 0; sb[i]; ++i) {      // 极简 atof
                if (sb[i] >= '0' && sb[i] <= '9') g_cfgScaleMul = g_cfgScaleMul * 10.0 + (sb[i] - '0');
                else if (sb[i] == '.') { ++i; double f = 0.1; 
                    for (; sb[i] >= '0' && sb[i] <= '9'; ++i, f /= 10.0)
                        g_cfgScaleMul += (sb[i] - '0') * f;
                    break; }
                else break;
            }
        }
        g_cfgQuality = (int)GetPrivateProfileIntA("font", "quality", 4, ini);
    }
    if (g_cfgQuality != 0 && g_cfgQuality != 1 && g_cfgQuality != 2 &&
        g_cfgQuality != 3 && g_cfgQuality != 4 && g_cfgQuality != 5) g_cfgQuality = 4;

    LogPrintf("[font] ini=%s (%s) file=\"%s\" face=\"%s\" quality=%d\r\n",
              ini, iniExists ? "found" : "MISSING, using defaults",
              g_cfgFontFile, g_cfgFontFace, g_cfgQuality);
    LogPrintf("[layout] xoff=%d yoff=%d mode=%d\r\n",
              g_cfgXoff, g_cfgYoff, g_cfgMapMode);

    // ---- 1) 私有加载 ttf ----
    char full[MAX_PATH]; char dir[MAX_PATH];
    CjkSidePath("", dir, sizeof(dir));
    my_strcpy(full, sizeof(full), dir);
    my_strcat(full, sizeof(full), g_cfgFontFile);

    WCHAR wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, full, -1, wpath, MAX_PATH);

    WCHAR ttfEn[LF_FACESIZE]; ttfEn[0] = 0;
    WCHAR ttfZh[LF_FACESIZE]; ttfZh[0] = 0;

    if (g_cfgFontFile[0] && GetFileAttributesA(full) != INVALID_FILE_ATTRIBUTES) {
        int added = AddFontResourceExW(wpath, FR_PRIVATE, 0);
        g_cfgTtfOk = (added > 0);
        TtfReadFamily(wpath, ttfEn, LF_FACESIZE, 0);        // 英文优先
        TtfReadFamily(wpath, ttfZh, LF_FACESIZE, 0x0804u);  // 简体中文名
        LogPrintf("[font] AddFontResourceExW(\"%s\") -> %d  (family en=\"%ls\" zh=\"%ls\")\r\n",
                  full, added, ttfEn, ttfZh);
    } else {
        LogPrintf("[font] ttf not found: \"%s\" -> fallback\r\n", full);
        my_strcpy(g_faceFallback, sizeof(g_faceFallback), "ttf missing");
    }

    // ---- 2) 决定族名（逐个候选实测）----
    WCHAR cand[4][LF_FACESIZE];
    int nc = 0;
    if (g_cfgFontFace[0]) {
        WCHAR t[LF_FACESIZE];
        MultiByteToWideChar(CP_ACP, 0, g_cfgFontFace, -1, t, LF_FACESIZE);
        for (int i = 0; i < LF_FACESIZE; ++i) cand[nc][i] = t[i];
        ++nc;
    }
    if (ttfEn[0]) { for (int i = 0; i < LF_FACESIZE; ++i) cand[nc][i] = ttfEn[i]; ++nc; }
    // ---- 字号补偿系数：cell/upem（见 TtfReadVert 上方的说明）----
    {
        int have = TtfReadVert(wpath);
        double autoScale = 1.0;
        if (have && g_fontUpem > 0 && (g_fontWinA + g_fontWinD) > 0)
            autoScale = (double)(g_fontWinA + g_fontWinD) / (double)g_fontUpem;
        if (autoScale < 0.5)  autoScale = 0.5;
        if (autoScale > 3.0)  autoScale = 3.0;
        g_fontScale = (g_cfgScaleMul > 0.01) ? g_cfgScaleMul : autoScale;
        LogPrintf("[font] metrics: upem=%d winAsc=%d winDesc=%d -> autoScale=%.4f"
                  " (ini scale=%.2f, using %.4f)\r\n",
                  g_fontUpem, g_fontWinA, g_fontWinD, autoScale, g_cfgScaleMul, g_fontScale);
        FlushFileBuffers(g_log);
    }
    if (ttfZh[0]) { for (int i = 0; i < LF_FACESIZE; ++i) cand[nc][i] = ttfZh[i]; ++nc; }
    // 文件名去扩展名当最后一个候选（有些 ttf 的族名就是文件名）
    {
        char stem[64]; int k = 0;
        for (int i = 0; g_cfgFontFile[i] && g_cfgFontFile[i] != '.' && k < 63; ++i) stem[k++] = g_cfgFontFile[i];
        stem[k] = 0;
        if (k > 0) {
            WCHAR t[LF_FACESIZE];
            MultiByteToWideChar(CP_ACP, 0, stem, -1, t, LF_FACESIZE);
            for (int i = 0; i < LF_FACESIZE; ++i) cand[nc][i] = t[i];
            ++nc;
        }
    }

    for (int i = 0; i < nc; ++i) {
        if (CjkFontFaceUsable(cand[i])) {
            for (int j = 0; j < LF_FACESIZE; ++j) g_faceW[j] = cand[i][j];
            g_cfgFaceOk = 1;
            break;
        }
    }
    if (!g_cfgFaceOk) {
        my_strcpy(g_faceFallback, sizeof(g_faceFallback), "no usable face, fallback SimSun");
        g_faceW[0] = L'S'; g_faceW[1] = L'i'; g_faceW[2] = L'm'; g_faceW[3] = L'S';
        g_faceW[4] = L'u'; g_faceW[5] = L'n'; g_faceW[6] = 0;
    }
    CjkWToA(g_faceW, g_faceA, LF_FACESIZE);
    LogPrintf("[font] using face = \"%s\" (ttfOk=%d faceOk=%d) %s\r\n",
              g_faceA, g_cfgTtfOk, g_cfgFaceOk, g_faceFallback);
    FlushFileBuffers(g_log);
}

// 取指定字号的 HFONT（带缓存，绝不因字号切换而重建）。
static HFONT ManFontFor(int px)
{
    for (int i = 0; i < g_fontN; ++i) {
        if (g_fontPxs[i] == px) { g_fontUsed[i] = ++g_fontTick; return g_fonts[i]; }
    }

    int slot = g_fontN;
    if (slot >= MAN_FONT_CACHE) {
        // 缓存满：淘汰最久未用的一个（先从 DC 摘掉再删，否则 DeleteObject 失败）
        slot = 0;
        for (int i = 1; i < MAN_FONT_CACHE; ++i)
            if (g_fontUsed[i] < g_fontUsed[slot]) slot = i;
        if (g_dibdc) SelectObject(g_dibdc, GetStockObject(SYSTEM_FONT));
        if (g_fonts[slot]) { DeleteObject(g_fonts[slot]); g_fonts[slot] = NULL; }
        if (g_fontSelPx == g_fontPxs[slot]) g_fontSelPx = -1;
    } else {
        ++g_fontN;
    }

    // ★ 字号补偿：GDI 的 lfHeight 是"单元格高度"，字体声明的 winAsc+winDesc 越大，
    //   em 就被压得越小（SimSun 的 cell=1.0em，思源黑体 cell=1.448em）。
    //   这里乘上 cell/upem，让**实际墨高**与 SimSun 时代一致。
    int pxUse = px;
    if (g_fontScale > 0.01) {
        pxUse = (int)((double)px * g_fontScale + 0.5);
        if (pxUse < px) pxUse = px;
        if (pxUse > px * 3) pxUse = px * 3;
    }
    g_fonts[slot] = CreateFontW(pxUse, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                GB2312_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, g_cfgQuality,
                                DEFAULT_PITCH, g_faceW);
    g_fontPxs[slot]  = px;
    g_fontUsed[slot] = ++g_fontTick;
    return g_fonts[slot];
}

// 把指定字号的字体选进 DIB DC（同字号不重复 SelectObject）。
static HFONT ManSelectFont(int px)
{
    HFONT f = ManFontFor(px);
    if (!f) return NULL;
    if (g_fontSelPx != px && g_dibdc) {
        SelectObject(g_dibdc, f);
        g_fontSelPx = px;
    }
    return f;
}

// ---------------------------------------------------------------- atlas 分配
//
// ★★★ 为什么必须有它（第 18 轮实机）：
//   之前所有文本项都上传到纹理的 (0,0) 区域。D3D9 是命令缓冲 + 异步执行，
//   GPU 真正执行 draw 时纹理里已经是**最后一次上传**的内容 ⇒ 一个界面里
//   只有最后画的那一项正确，其余的按各自 w/h 取样到黑底或碎片，看起来就是
//   "不显示"；而每帧绘制顺序一变，可见的那一项就换人 ⇒ **闪烁**。
//   实测主菜单 6 项里只有最后画的「新游戏」可见 —— 与日志完全一致。
//
// 分配方式是 shelf（行内向右排，放不下换行）。单调前进、**绕回可重用**：
// 绕回时早先那些 draw 早已执行完，所以重用是安全的；只要
// 「一帧内的项数 ≤ 容量」就绝不会互相覆盖。
static int g_allocX = 0, g_allocY = 0, g_allocRowH = 0;

static void ManAtlasReset(void)
{
    g_allocX = 0; g_allocY = 0; g_allocRowH = 0;
}

static int ManAtlasAlloc(int w, int h, int* ox, int* oy)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > MAN_ATLAS_W || h > MAN_ATLAS_H) return 0;

    if (g_allocX + w > MAN_ATLAS_W) {          // 本行放不下 → 换行
        g_allocY += g_allocRowH;
        g_allocX  = 0;
        g_allocRowH = 0;
    }
    if (g_allocY + h > MAN_ATLAS_H) return 0;  // 整张图满了

    *ox = g_allocX;
    *oy = g_allocY;
    g_allocX += w;
    if (h > g_allocRowH) g_allocRowH = h;
    return 1;
}

// 惰性初始化：DIB + 纹理。任何一步失败都返回 0 交给 D3DX 兜底。
static int ManEnsure(void* dev, int px)
{
    if (px < 6)  px = 6;
    if (px > 96) px = 96;

    if (g_manFatal || g_manFail >= 8) return 0;
    CjkLoadFontConfig();       // 首次进入时读 update\chs.ini 并私有加载 ttf

    // ★ 纹理必须挂在**当前有效设备**上。D3D 设备丢失/重置后旧纹理对象
    //   就成了野指针，第 10 轮崩在 `dvt[57]`（+0xE4）就是这类问题。
    //   每次进来都重新确认「设备有效 + 纹理属于它」，否则全部丢弃重建。
    if (!dev || !LooksLikeDevice(dev)) { g_tex = NULL; g_texDev = NULL; return 0; }
    if (g_tex && g_texDev != dev) { g_tex = NULL; g_texDev = NULL; }
    if (g_tex) {
        // 纹理对象自身也确认一下（IsBadReadPtr 查虚表可读）
        void** tvt = *(void***)g_tex;
        if (!tvt || IsBadReadPtr(tvt, 0x80)) { g_tex = NULL; g_texDev = NULL; }
    }
    // 设备换了 ⇒ 槽位探测结果作废（不同驱动的虚表布局未必相同），
    // 状态块也必须重建（它属于旧设备）
    if (g_texProbeDev != dev) { g_texProbeDone = 0; g_texProbeDev = dev; }


    // ---- 字体不在这里管：改由 ManSelectFont() 按字号缓存（见上）----

    // ---- DIB（自上而下，行序与纹理一致）----
    if (!g_dibdc) {
        HDC sdc = GetDC(NULL);
        g_dibdc = CreateCompatibleDC(sdc);
        if (sdc) ReleaseDC(NULL, sdc);
        if (!g_dibdc) { g_manFatal = 1; return 0; }

        BITMAPINFO bi;
        ZeroBuf(&bi, sizeof(bi));
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = MAN_ATLAS_W;
        bi.bmiHeader.biHeight      = -(long)MAN_ATLAS_H;   // 负 = top-down
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        g_dibbm = CreateDIBSection(g_dibdc, &bi, DIB_RGB_COLORS, &g_dibbits, NULL, 0);
        if (!g_dibbm) { g_manFatal = 1; return 0; }
        SelectObject(g_dibdc, g_dibbm);
        SetBkMode(g_dibdc, OPAQUE);
        SetBkColor(g_dibdc, RGB(0, 0, 0));
        SetTextColor(g_dibdc, RGB(255, 255, 255));
        g_fontSelPx = -1;   // 新 DC 还没选入任何字体，交给 ManSelectFont
    }

    // ---- 纹理 ----
    // ★★★ 这里不用 IDirect3DDevice9::CreateTexture（虚表索引 22）——
    //   实测该槽位返回 hr=0xDEADBEEF，是错的（引擎走 D3DXLoadSurfaceFromMemory
    //   建纹理，exe 里没有直接调用点可反推，"按接口层次数出来"这招在这儿不成立）。
    //   改用 **导出函数** D3DXCreateTexture：GetProcAddress 拿到，零索引风险。
    if (!g_tex || g_texDev != dev) {
        if (g_tex) {
            void** vt = *(void***)g_tex;
            typedef long (__stdcall *Rel_t)(void*);
            Rel_t rel = (Rel_t)vt[2];
            if (InD3d9(rel)) rel(g_tex);
            g_tex = NULL;
        }

        if (!g_pCreateTex) {
            g_d3dx = GetModuleHandleA("d3dx9_43.dll");
            if (!g_d3dx) g_d3dx = LoadLibraryA("d3dx9_43.dll");
            if (g_d3dx) g_pCreateTex = (D3DXCreateTexture_t)GetProcAddress(g_d3dx, "D3DXCreateTexture");
        }
        if (!g_pCreateTex) {
            g_manFatal = 1;
            LogPrintf("[man] D3DXCreateTexture not found (d3dx9_43=%p)\r\n", g_d3dx);
            FlushFileBuffers(g_log);
            return 0;
        }

        void* t = NULL;
        long hr = -3;
        __try {
            // ★ D3DXCreateTexture 只有 8 个参数（没有 pSharedHandle）。
            //   旧代码多压了一个 NULL —— __stdcall 下 callee 只弹 32 字节，
            //   会在栈上留 4 字节残留。虽然不影响返回值，但属于 UB，去掉。
            hr = g_pCreateTex(dev, MAN_ATLAS_W, MAN_ATLAS_H, 1,
                              0, D3DFMT_A8R8G8B8_, D3DPOOL_MANAGED_, &t);
        } __except (CjkTrap("D3DXCreateTexture", GetExceptionInformation())) {
            hr = -4;
        }
        if (hr != 0 || !t) {
            LogPrintf("[man] D3DXCreateTexture %dx%d A8R8G8B8 MANAGED -> hr=0x%08lX tex=%p\r\n",
                      MAN_ATLAS_W, MAN_ATLAS_H, (unsigned long)hr, t);
            FlushFileBuffers(g_log);
            g_manFail++;
            return 0;
        }

        // 校验：GetLevelDesc 回读尺寸 —— 纯读取，索引错了最多返回垃圾，不破坏状态。
        // ★ 这只是**诊断信息**，不是准入条件：GetLevelDesc 的槽位同样没实测过
        //   （第 7 轮就因为它索引不对而误杀了一个好纹理）。
        //   真正的准入是下面 CjkProbeTex() 的 LockRect 探测 —— 那个用返回值判定，
        //   跑不通就自然画不出东西，不需要在这里拿不确定的索引去否决。
        {
            void** tvt = *(void***)t;
            if (IsBadReadPtr(tvt, 0x40)) {
                LogPrintf("[man] D3DXCreateTexture returned obj with unreadable vtable %p\r\n", tvt);
                g_manFatal = 1;
                return 0;
            }
            TexGetLevelDesc_t gd = (TexGetLevelDesc_t)tvt[VTI_TEX_GETLEVELDESC];
            if (gd && InD3d9(gd)) {
                unsigned char raw[64];
                ZeroBuf(raw, sizeof(raw));
                long hr2 = -3;
                __try { hr2 = gd(t, 0, raw); }
                __except (CjkTrap("IDirect3DTexture9::GetLevelDesc", GetExceptionInformation())) { hr2 = -4; }
                // D3DDESC9 头三个 DWORD 就是 Width/Height/Size(Pitch)。
                // 直接按 DWORD 读，不依赖我对结构偏移的假设是否正确。
                unsigned w0 = 0, h0 = 0, p0 = 0;
                if (hr2 == 0) {
                    __try {
                        w0 = *(const unsigned*)(raw + 0);
                        h0 = *(const unsigned*)(raw + 4);
                        p0 = *(const unsigned*)(raw + 8);
                    }
                    __except (CjkTrap("desc read", GetExceptionInformation())) { }
                }
                LogPrintf("[man] D3DXCreateTexture ok tex=%p  GetLevelDesc hr=0x%08lX "
                          "w=%u h=%u pitch=%u (want %dx%d)\r\n",
                          t, (unsigned long)hr2, w0, h0, p0, MAN_ATLAS_W, MAN_ATLAS_H);
            } else {
                LogPrintf("[man] D3DXCreateTexture ok tex=%p  (GetLevelDesc slot %d unavailable)\r\n",
                          t, VTI_TEX_GETLEVELDESC);
            }
            FlushFileBuffers(g_log);
        }

        g_tex    = t;
        g_texDev = dev;
        ManAtlasReset();      // 新纹理内容全黑，分配指针回到原点
    }

    // ★ 不再把 g_hfont 作为前置条件 —— 字体是按需缓存的，ManSelectFont 里判
    return (g_dibdc && g_dibbm && g_tex) ? 1 : 0;
}

// ---------------------------------------------------------------- 四边形批次
//
// ★★★ 第 21 轮对照实验（都是实机验证过的）：
//   · 一次 DrawPrimitiveUP 画 2 个四边形                      → 两个都出现 ✓
//   · 两次调用，**紧挨着**、中间没有任何状态改动                → 两个都出现 ✓
//   · 两次调用，**中间隔着引擎的绘制 / 状态改动**               → 只剩最后一个 ✗
//   ⇒ 本作这个 D3D9 实现下，"被别的绘制隔开的" DrawPrimitiveUP
//     只有最后一笔能落屏 —— 这就是"一个界面只显示一项、还闪烁"的根因。
//   ⇒ 解法：一帧内所有中文四边形**攒成一批**，状态只设置一次，
//     用**一次** DrawPrimitiveUP 全部画出去，然后还原一次。
struct ManQuad { float x0, y0, x1, y1, u0, v0, u1, v1; };

#define MAN_MAXQUAD 512
static ManQuad  g_quads[MAN_MAXQUAD];
static int      g_nQuads   = 0;
static ManVert  g_qverts[MAN_MAXQUAD * 6];
static unsigned long g_batchDrawn = 0;

// ---- 帧边界（第 22 轮重写）----
//   ★★ 旧实现只看"签名重复"，在有**同签名文本项**的界面（引擎按两遍画同一行、
//      闪烁效果等）会在一帧中间误判新帧 ⇒ 该帧先前登记的项目被整批丢掉 ⇒
//      **那几行整帧空白**（用户报的"难度等级按键后空白 / 结算屏幕空白"）。
//   现在以**时间间隔**为主判据：同一帧内的文本项是连着来的（间隔 0~1ms），
//      跨帧间隔 ≈ 帧长（>= 8ms）。签名重复只在间隔也明显（>2ms）时作辅助判据。
static unsigned long g_lastEmitTick = 0;
static unsigned long g_lastGap      = 0;
static unsigned long g_gapHist[5];        // <=1 / <=3 / <=8 / <=16 / >16 ms
static int           g_logAllEmit   = 0;  // update\chs_logall.txt 存在时逐项打日志
static int           g_logAllChecked= 0;
static unsigned long g_frameNewByGap = 0;
static unsigned long g_frameNewBySig = 0;

// ---- 帧边界判定 ----
// ★ 实测本作引擎**不调 BeginScene / EndScene / Present**（dev 与 swapchain 全是 0），
//   拿不到任何帧回调。但引擎每帧画的文本项及其位置是固定的 ⇒
//   **"同一个 (文本,位置,字号) 在本批里已经出现过" = 新的一帧开始了**。
#define MAN_MAXSIG 128
static unsigned long g_sig[MAN_MAXSIG];
static int g_nSig = 0;

static unsigned long ManSig(const WCHAR* t, int n, int x, int y, int h)
{
    unsigned long v = 2166136261ul;
    for (int i = 0; i < n && i < 64; ++i) { v ^= (unsigned long)t[i]; v *= 16777619ul; }
    v ^= (unsigned long)x; v *= 16777619ul;
    v ^= (unsigned long)y; v *= 16777619ul;
    v ^= (unsigned long)h; v *= 16777619ul;
    return v;
}
static int  ManSigSeen(unsigned long v) { for (int i = 0; i < g_nSig; ++i) if (g_sig[i] == v) return 1; return 0; }
static void ManSigAdd(unsigned long v)  { if (g_nSig < MAN_MAXSIG) g_sig[g_nSig++] = v; }
static void ManQuadReset(void)          { g_nSig = 0; g_nQuads = 0; }

static int ManEmitQuad(float x0, float y0, float x1, float y1,
                       float u0, float v0, float u1, float v1)
{
    if (g_nQuads >= MAN_MAXQUAD) return 0;
    ManQuad* q = &g_quads[g_nQuads++];
    q->x0 = x0; q->y0 = y0; q->x1 = x1; q->y1 = y1;
    q->u0 = u0; q->v0 = v0; q->u1 = u1; q->v1 = v1;
    return 1;
}

// 画一行。align: 1=水平居中 2=右对齐（相对锚点 ax）
static long ManDrawLine(void* dev, const WCHAR* s, int ax, int ay, int px, int align)
{
    if (!s || !dev) return -1;
    // ★★ 设备指针"非空"≠"有效"。第 10 轮崩在这里之后：
    //   dev 是个非空但已失效的野指针，`*(void***)dev` 得 0，
    //   后面读 `dvt[57]`（+0xE4）就 AV 了。所以在做任何虚表操作前
    //   先跑一遍 LooksLikeDevice（它会验证虚表前 4 项都落在 d3d9.dll 内）。
    if (!LooksLikeDevice(dev)) return -1;
    if (!ManEnsure(dev, px)) return -1;
    if (!ManSelectFont(px)) return -1;

    // ★ 留痕改成**倒计数**：旧写法 `g_manDraws < 2` 一旦成功计数不增
    //   （或提前 return）就永远为真，会无限刷日志 + 每次 FlushFileBuffers，
    //   实测把游戏拖到卡死。倒计数保证最多留痕 4 次。
    int trace = 0;
    if (g_traceLeft > 0) { --g_traceLeft; trace = 1; }

    // ★★ 扫描字符串前必须先确认这块内存可读。
    //   第 9 轮实测崩在这里（`lea eax,[esi+4]` / `cmp word ptr [eax-4],0`）：
    //   调用方传的是 `it->t + pos`，pos 由"找 NUL"推进，一旦某行没有 NUL
    //   终止符，pos 就会冲出 CJKQ_CHARS 边界 → 野指针 → READ 违例。
    //   这里对整个可扫描区间一次性做可读性校验，越界直接放弃这一行。
    if (IsBadReadPtr(s, 2)) return -1;
    int n = 0;
    while (n < MAN_MAXSTR && s[n]) {
        // 每 16 个字查一次可读性（IsBadReadPtr 按页判定，够用且不慢）
        if ((n & 15) == 0 && n > 0 && IsBadReadPtr(s + n, 2)) return -1;
        ++n;
    }
    if (n == 0) return -1;
    // 扫到上限仍未见 NUL ⇒ 这行没有终止符，丢弃（否则 ExtTextOutW 会读越界）
    if (n >= MAN_MAXSTR) return -1;

    // ---- 帧边界（见 g_lastEmitTick 处的说明）----
    unsigned long gap;
    {
        unsigned long now = GetTickCount();
        gap = g_lastEmitTick ? (unsigned long)(now - g_lastEmitTick) : 0xFFFFFFFFul;
        g_lastGap = gap;
        g_lastEmitTick = now;
        if      (gap <= 1)  g_gapHist[0]++;
        else if (gap <= 3)  g_gapHist[1]++;
        else if (gap <= 8)  g_gapHist[2]++;
        else if (gap <= 16) g_gapHist[3]++;
        else                g_gapHist[4]++;

        // ★ 第 22 轮起：帧边界交给**帧末钩子**（Present 之前统一画一次），
        //   这里只保留 gap 直方图做诊断，不再用时间/签名去猜帧。
        (void)ax; (void)ay; (void)px;
    }

    // ---- 诊断开关：update\chs_logall.txt 存在时逐项留痕（默认关）----
    if (!g_logAllChecked) {
        g_logAllChecked = 1;
        char q[MAX_PATH];
        CjkSidePath("chs_logall.txt", q, sizeof(q));
        g_logAllEmit = (GetFileAttributesA(q) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
        LogPrintf("[emit] log-all = %d\r\n", g_logAllEmit);
        FlushFileBuffers(g_log);
    }
    if (g_logAllEmit) {
        char tb[120];
        CjkWideToLog(s, tb, sizeof(tb));
        LogPrintf("[emit] gap=%lu n=%d t=\"%s\" at=(%d,%d) px=%d\r\n", gap, n, tb, ax, ay, px);
        FlushFileBuffers(g_log);
    }

    SIZE sz;
    ZeroBuf(&sz, sizeof(sz));
    if (!GetTextExtentPoint32W(g_dibdc, s, n, &sz)) return -1;

    int w = sz.cx + 2;
    int h = sz.cy + 2;
    if (w < 2) w = 2;
    if (h < 2) h = 2;
    if (w > MAN_ATLAS_W - MAN_SLOT_PAD * 2) w = MAN_ATLAS_W - MAN_SLOT_PAD * 2;
    if (h > MAN_ATLAS_H - MAN_SLOT_PAD * 2) h = MAN_ATLAS_H - MAN_SLOT_PAD * 2;

    int left = ax;
    if (align & 1)      left -= w / 2;
    else if (align & 2) left -= w;

    // ---- 在 atlas 里取一块**独占**区域（含四周 1px 透明边）----
    int aw = w + MAN_SLOT_PAD * 2;
    int ah = h + MAN_SLOT_PAD * 2;
    int sx = 0, sy = 0;
    if (!ManAtlasAlloc(aw, ah, &sx, &sy)) {
        ManAtlasReset();                          // 绕回一圈（旧 draw 早已执行完）
        if (!ManAtlasAlloc(aw, ah, &sx, &sy)) return -1;
    }

    // ---- GDI 画进 DIB 的这块槽位。ETO_OPAQUE 只清我们这一块 ----
    //   文字位置相对槽左上是 (PAD+1, PAD+1)：PAD 是透明边，多出的 1 是
    //   GetTextExtentPoint32W 给的尺寸偏紧、留 1px 余量。
    RECT rc;
    rc.left   = sx;              rc.top    = sy;
    rc.right  = sx + aw;         rc.bottom = sy + ah;
    if (!ExtTextOutW(g_dibdc, sx + MAN_SLOT_PAD + 1, sy + MAN_SLOT_PAD + 1,
                     ETO_OPAQUE | ETO_CLIPPED, &rc, s, n, NULL)) return -1;

    // ---- 上传：把灰度当 alpha，RGB 拉满白（顶点色负责真正颜色）----
    // ★ 这是**唯一**需要读 DIB 像素的地方，所以 DIB 的可读性在这里一次性确认。
    //   （第 9 轮崩在字符串扫描，与此无关；但 DIB 若因设备重置失效，
    //    这里会是下一个崩溃点，所以顺手加守卫。）
    if (!g_dibbits || IsBadReadPtr(g_dibbits, 4)) return -1;

    TexLockRect_t   lk  = NULL;
    TexUnlockRect_t ulk = NULL;
    CjkProbeTex(g_tex, &lk, &ulk);
    if (!lk || !ulk) return -1;

    ManLockedRect lr;
    ZeroBuf(&lr, sizeof(lr));
    RECT lr0;
    lr0.left = sx;        lr0.top    = sy;
    lr0.right = sx + aw;  lr0.bottom = sy + ah;

    long hr = -3;
    if (trace) { LogPrintf("[man] traceA: LockRect...\r\n"); FlushFileBuffers(g_log); }
    __try { hr = lk(g_tex, 0, &lr, &lr0, 0); }
    __except (CjkTrap("IDirect3DTexture9::LockRect", GetExceptionInformation())) { hr = -4; }
    if (hr != 0 || !lr.pBits) return hr;

    // DIB 是 32bpp 且宽度=MAN_ATLAS_W ⇒ 每行恰好 MAN_ATLAS_W*4 字节
    int lit = 0;
    for (int y = 0; y < ah; ++y) {
        const unsigned char* src = (const unsigned char*)g_dibbits
                                 + (size_t)(sy + y) * (MAN_ATLAS_W * 4) + (size_t)sx * 4;
        unsigned char* dst = (unsigned char*)lr.pBits + (size_t)y * (size_t)lr.Pitch;
        if (IsBadReadPtr(src, 4) || IsBadReadPtr(dst, 4)) {
            __try { ulk(g_tex, 0); }
            __except (CjkTrap("unlock after bad ptr", GetExceptionInformation())) { }
            g_manFail++;
            return -1;
        }
        for (int x = 0; x < aw; ++x) {
            unsigned char lum = src[x * 4];        // 32bpp DIB 是 B,G,R,X；灰度取任一通道
            if (lum) ++lit;                        // 诊断：非零像素数
            dst[x * 4 + 0] = 0xFF;                 // B
            dst[x * 4 + 1] = 0xFF;                 // G
            dst[x * 4 + 2] = 0xFF;                 // R
            dst[x * 4 + 3] = lum;                  // A
        }
    }
    __try { ulk(g_tex, 0); }
    __except (CjkTrap("IDirect3DTexture9::UnlockRect", GetExceptionInformation())) { }

    if (trace) {
        LogPrintf("[man] traceB: text w=%d h=%d lit=%d slot=(%d,%d %dx%d) drawAt=(%d,%d)\r\n",
                  w, h, lit, sx, sy, aw, ah, left, ay);
        FlushFileBuffers(g_log);
    }

    // ---- 四边形（XYZRHW：已经是屏幕像素，不再做任何变换）----
    // 槽左上角对应屏幕 (left - (PAD+1), ay - (PAD+1))：文字在槽内偏移 PAD+1。
    float ox = (float)(left - (MAN_SLOT_PAD + 1));
    float oy = (float)(ay   - (MAN_SLOT_PAD + 1));
    float u0 = (float)sx / (float)MAN_ATLAS_W;
    float v0 = (float)sy / (float)MAN_ATLAS_H;
    float u1 = (float)(sx + aw) / (float)MAN_ATLAS_W;
    float v1 = (float)(sy + ah) / (float)MAN_ATLAS_H;

    if (!ManEmitQuad(ox, oy, ox + (float)aw, oy + (float)ah, u0, v0, u1, v1))
        return -1;

    g_manLastHr = 0;
    ++g_manDraws;

    // ★★★ 绘制时机（第 22 轮改造）：
    //   正常情况下**什么都不做** —— 四边形留在批次里，由帧末钩子（Present 之前）
    //   一次性画出去：那时引擎已画完世界/HUD，我们的文本不会再被盖住，
    //   而且一帧只有一次 DrawPrimitiveUP（避开"被别的绘制隔开就只剩最后一笔"）。
    //   帧末钩子装不上时才退回"每项立即重画整批"的旧行为（能用但会闪）。
    if (!g_frameHookOk) {
        ManDrawQuads(dev);
    } else {
        // 保险：帧末钩子装了却迟迟不触发（异常路径），超过 150ms 自己画一次
        unsigned long now2 = GetTickCount();
        if (g_lastFrameTick && (unsigned long)(now2 - g_lastFrameTick) > 150) {
            ManDrawQuads(dev);
            g_nQuads = 0;
            ManAtlasReset();
            g_lastFrameTick = now2;
        }
    }
    return 0;
}

// ================================================================ 批次绘制
// 把攒下的四边形一次性画出去。**状态保存 / 设置 / 还原各只做一次**，
// 全部四边形走同一条 DrawPrimitiveUP。
static long ManDrawQuads(void* dev)
{
    if (!dev) return -1;
    if (g_nQuads <= 0) return -1;
    if (!LooksLikeDevice(dev)) return -1;

    void** dvt = *(void***)dev;
    if (!dvt || IsBadReadPtr(dvt, 0x1B0)) return -1;

    SetRenderState_t   srs  = (SetRenderState_t)dvt[VTI_SETRENDERSTATE];
    SetTSS_t           sts  = (SetTSS_t)dvt[VTI_SETTEXTURESTAGESTATE];
    SetSamplerState_t  sss  = (SetSamplerState_t)dvt[VTI_SETSAMPLERSTATE];
    SetTexture_t       stx  = (SetTexture_t)dvt[VTI_SETTEXTURE];
    SetFVF_t           sfvf = (SetFVF_t)dvt[VTI_SETFVF];
    DrawUP_t           dup  = (DrawUP_t)dvt[VTI_DRAWPRIMITIVEUP];
    void* getRS  = dvt[VTI_GETRENDERSTATE];
    void* getFVF = dvt[VTI_GETFVF];
    void* getTSS = dvt[VTI_GETTEXTURESTAGESTATE];
    void* getSS  = dvt[VTI_GETSAMPLERSTATE];
    void* getTX  = dvt[VTI_GETTEXTURE];

    if (!srs || !sts || !stx || !sfvf || !dup) return -1;
    if (!InD3d9(srs) || !InD3d9(sts) || !InD3d9(stx) ||
        !InD3d9(sfvf) || !InD3d9(dup)) return -1;
    if (sss && !InD3d9(sss)) sss = NULL;

    // ★ 读不到原状态就**不画** —— 绝不"改完不还"
    if (g_gettersFail == -1) return -1;
    if (!getRS || !getFVF || !InD3d9(getRS) || !InD3d9(getFVF)) {
        g_gettersFail = -1;
        LogPrintf("[man] state getters unavailable -> CJK draw disabled\r\n");
        FlushFileBuffers(g_log);
        return -1;
    }

    // ---- 构造顶点（TRIANGLELIST：每个四边形 6 个顶点 = 2 个三角形）----
    for (int i = 0; i < g_nQuads; ++i) {
        const ManQuad* q = &g_quads[i];
        ManVert* v = &g_qverts[i * 6];
        for (int k = 0; k < 6; ++k) { v[k].z = 0.0f; v[k].rhw = 1.0f; v[k].d = 0xFFFFFFFFul; }
        v[0].x = q->x0; v[0].y = q->y0; v[0].u = q->u0; v[0].v = q->v0;
        v[1].x = q->x1; v[1].y = q->y0; v[1].u = q->u1; v[1].v = q->v0;
        v[2].x = q->x1; v[2].y = q->y1; v[2].u = q->u1; v[2].v = q->v1;
        v[3].x = q->x0; v[3].y = q->y0; v[3].u = q->u0; v[3].v = q->v0;
        v[4].x = q->x1; v[4].y = q->y1; v[4].u = q->u1; v[4].v = q->v1;
        v[5].x = q->x0; v[5].y = q->y1; v[5].u = q->u0; v[5].v = q->v1;
    }
    int nQuads = g_nQuads;      // ★ 不清空：本帧后续会重画整批

    static const int kRs[8]  = { D3DRS_ZENABLE_, D3DRS_ALPHABLENDENABLE_,
                                 D3DRS_SRCBLEND_, D3DRS_DESTBLEND_,
                                 D3DRS_ALPHATESTENABLE_, D3DRS_CULLMODE_,
                                 D3DRS_LIGHTING_, D3DRS_FOGENABLE_ };
    static const int kTss[6] = { D3DTSS_COLOROP_, D3DTSS_COLORARG1_, D3DTSS_COLORARG2_,
                                 D3DTSS_ALPHAOP_, D3DTSS_ALPHAARG1_, D3DTSS_ALPHAARG2_ };
    static const int kSs[3]  = { D3DSAMP_MINFILTER_, D3DSAMP_MAGFILTER_, D3DSAMP_MIPFILTER_ };

    unsigned long svRs[8], svTss[6], svSs[3], svFvf = 0;
    void*         svTex = NULL;

    for (int i = 0; i < 8; ++i)
        CjkSafeCall3(getRS, dev, (void*)(long)kRs[i], &svRs[i]);
    CjkSafeCall3(getFVF, dev, &svFvf, NULL);
    if (getTSS && InD3d9(getTSS))
        for (int i = 0; i < 6; ++i)
            CjkSafeCall5(getTSS, dev, NULL, (void*)(long)kTss[i], &svTss[i], NULL);
    if (getSS && InD3d9(getSS))
        for (int i = 0; i < 3; ++i)
            CjkSafeCall5(getSS, dev, NULL, (void*)(long)kSs[i], &svSs[i], NULL);
    if (getTX && InD3d9(getTX))
        CjkSafeCall3(getTX, dev, NULL, &svTex);

    long hr = -3;
    __try {
        srs(dev, D3DRS_ZENABLE_, 0);
        srs(dev, D3DRS_ALPHABLENDENABLE_, 1);
        srs(dev, D3DRS_SRCBLEND_, D3DBLEND_SRCALPHA_);
        srs(dev, D3DRS_DESTBLEND_, D3DBLEND_INVSRCALPHA_);
        srs(dev, D3DRS_ALPHATESTENABLE_, 0);
        srs(dev, D3DRS_CULLMODE_, D3DCULL_NONE_);
        srs(dev, D3DRS_LIGHTING_, 0);
        srs(dev, D3DRS_FOGENABLE_, 0);
        sts(dev, 0, D3DTSS_COLOROP_,   D3DTOP_MODULATE_);
        sts(dev, 0, D3DTSS_COLORARG1_, D3DTA_TEXTURE_);
        sts(dev, 0, D3DTSS_COLORARG2_, D3DTA_DIFFUSE_);
        sts(dev, 0, D3DTSS_ALPHAOP_,   D3DTOP_MODULATE_);
        sts(dev, 0, D3DTSS_ALPHAARG1_, D3DTA_TEXTURE_);
        sts(dev, 0, D3DTSS_ALPHAARG2_, D3DTA_DIFFUSE_);
        if (sss) {
            sss(dev, 0, D3DSAMP_MINFILTER_, D3DTEXF_LINEAR_);
            sss(dev, 0, D3DSAMP_MAGFILTER_, D3DTEXF_LINEAR_);
            sss(dev, 0, D3DSAMP_MIPFILTER_, D3DTEXF_NONE_);
        }
        stx(dev, 0, g_tex);
        sfvf(dev, MAN_FVF);
        hr = dup(dev, D3DPT_TRIANGLELIST_, nQuads * 2, g_qverts, 28);
    } __except (CjkTrap("batched quad draw", GetExceptionInformation())) {
        hr = -4;
    }

    // ---- 还原（逆序 & 逐项）----
    __try {
        if (getTX && InD3d9(getTX) && stx) stx(dev, 0, svTex);
        sfvf(dev, svFvf);
        if (sss) for (int i = 0; i < 3; ++i) sss(dev, 0, kSs[i],  svSs[i]);
        for (int i = 0; i < 6; ++i)          sts(dev, 0, kTss[i], svTss[i]);
        for (int i = 0; i < 8; ++i)          srs(dev, kRs[i], svRs[i]);
    } __except (CjkTrap("state restore", GetExceptionInformation())) { }

    g_manLastHr = hr;
    ++g_batchDrawn;
    if (g_batchDrawn <= 6) {
        LogPrintf("[man] batch #%lu quads=%d hr=0x%08lX\r\n",
                  g_batchDrawn, nQuads, (unsigned long)hr);
        FlushFileBuffers(g_log);
    }
    return hr;
}


// ================================================================ 帧末绘制
// 由 CjkFrameEndTrampoline 在引擎 Present 之前调用（每帧一次）。
extern "C" void __cdecl ManFrameEndFlush(void* engineScreen)
{
    ++g_frameEndCalls;
    g_lastFrameTick = GetTickCount();

    void* dev = NULL;
    if (engineScreen && !IsBadReadPtr((char*)engineScreen + OFF_EngineDev, 4)) {
        void* d = *(void**)((char*)engineScreen + OFF_EngineDev);
        if (d && LooksLikeDevice(d)) dev = d;
    }
    if (g_frameDevLogged != dev) {
        g_frameDevLogged = dev;
        LogPrintf("[frame] engine device=%p  g_device=%p  same=%d  (call #%lu)\r\n",
                  dev, g_device, (dev == g_device) ? 1 : 0, g_frameEndCalls);
        FlushFileBuffers(g_log);
    }
    if (!dev) dev = CjkActiveDevice();

    if (g_nQuads > 0) {
        int n = g_nQuads;
        if (ManDrawQuads(dev) == 0) g_frameDrawnQuads += (unsigned long)n;
        g_nQuads = 0;
        ManAtlasReset();        // 新一帧：atlas 从原点重新分配
    }
    if (g_frameEndCalls <= 6 || (g_frameEndCalls % 500) == 0) {
        LogPrintf("[frame] end #%lu pending=%d drawnQuads=%lu\r\n",
                  g_frameEndCalls, g_nQuads, g_frameDrawnQuads);
        FlushFileBuffers(g_log);
    }
}

// ---------------------------------------------------------------- 坐标映射
//
// ★ 引擎坐标不是屏幕像素：实测锚点大量取负值（-288..-266 / -101..358），
//   而整块主菜单全在这个区间 —— 若原样当屏幕坐标画，整排菜单都在屏幕外。
//   sub_426680 里从对齐到 `call [eax+0Ch]` 之间没有任何平移指令，
//   所以偏移/缩放发生在字形渲染器内部（多半是正交投影 + 原点在屏幕中心）。
//
//   两种可能，运行时自动判：
//     ① 设备设了正交投影（PROJECTION 非单位阵）→ 走矩阵变换
//     ② 单位阵（渲染器自己加中心偏移）→ 屏幕 = 逻辑 + 视口中心
static unsigned long g_vpX = 0, g_vpY = 0, g_vpW = 0, g_vpH = 0;
static int           g_mapMode   = 0;    // 0=未定 1=中心原点 2=矩阵
// ★ 映射结果缓存：CjkFlush 是**每个文本块**调一次 CjkUpdateMapping，
//   而它要 GetTransform×3 + GetViewport（缺视口时还要 GetBackBuffer+Release）。
//   投影矩阵/视口只在分辨率或设备变化时才变 —— 同一设备上最多每秒复查一次。
static void*         g_mapDev    = NULL;
static unsigned long g_mapTick   = 0;
static float         g_xf[16];
static int           g_mapLogged = 0;

static void MatMul(float* o, const float* a, const float* b)
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[r * 4 + k] * b[k * 4 + c];
            o[r * 4 + c] = s;
        }
}

static float g_liveConstX = 0.0f, g_liveConstY = 0.0f;   // 诊断：实时矩阵平移项
static float g_liveScaleX = 0.0f, g_liveScaleY = 0.0f;
static void CjkUpdateMappingEx(void* dev, int force);
static void CjkUpdateMapping(void* dev) { CjkUpdateMappingEx(dev, 0); }
// ---- 坐标映射诊断（update\chs_mapdiag.txt）----
//   作用：实时重抓矩阵（不走 1 秒缓存）并逐项打印
//   `[mapdiag] live const/scale | logic -> screen`，用来判断"游戏内字跟着相机动"
//   到底是**矩阵抓错了**还是**逻辑坐标本身在动**。
static int   g_mapDiag     = 0;
static int   g_mapDiagChk  = 0;
static int   g_mapDiagN    = 0;
static float g_mapDiagLastX = 9999.0f, g_mapDiagLastY = 9999.0f;

static void CjkUpdateMappingEx(void* dev, int force)
{
    if (!dev) return;
    if (!force) {
        // ★★★ 缓存窗口从 1000ms 改到 **2ms**（第 23 轮实测结论）：
        //   实测游戏内 HUD 文本会随相机/鼠标移动，而菜单不动 —— 根因就是原来
        //   「1 秒才重抓一次变换」：相机一移动，抓到的矩阵就是 1 秒前那个（甚至
        //   是另一条渲染通路的矩阵），套到当前坐标上 ⇒ 文本跟着相机跑。
        //   改成 2ms：同一帧内不会重复抓（一帧 16ms），跨帧必然重抓 ⇒ 永远是最新的。
        //   GetTransform 本身很便宜，一帧最多 3 次 × 几个文本块，开销可忽略。
        if (g_mapMode && g_mapDev == dev) {
            unsigned long now = GetTickCount();
            if ((unsigned long)(now - g_mapTick) < 2u) return;
        }
    } else {
        // 实时模式（诊断/每块重抓）只更新矩阵，不动缓存时间戳
        g_mapTick = GetTickCount();
    }
    void** dvt = *(void***)dev;
    // 0x1B0 = 108*4，覆盖本文件用到的最大槽位（SetVertexShader=107）
    if (!dvt || IsBadReadPtr(dvt, 0x1B0)) return;

    GetViewport_t gvp = (GetViewport_t)dvt[VTI_GETVIEWPORT];   // 48（47 是 SetViewport）
    if (gvp && InD3d9(gvp)) {
        ManVP vp;
        ZeroBuf(&vp, sizeof(vp));
        __try { gvp(dev, &vp); }
        __except (CjkTrap("IDirect3DDevice9::GetViewport", GetExceptionInformation())) { }
        g_vpX = vp.x; g_vpY = vp.y; g_vpW = vp.w; g_vpH = vp.h;
    }

    // 视口拿不到就退一步问后缓冲尺寸
    if (!g_vpW || !g_vpH) {
        GetBackBuffer_t gbb = (GetBackBuffer_t)dvt[VTI_GETBACKBUFFER];
        if (gbb && InD3d9(gbb)) {
            void* s = NULL;
            if (gbb(dev, 0, 0, 0, &s) == 0 && s) {
                ManSurfDesc sd;
                ZeroBuf(&sd, sizeof(sd));
                void** svt = *(void***)s;
                // ★ IDirect3DSurface9: 0..2 IUnknown, 3 GetDevice, 4 SetPrivateData,
                //   5 GetPrivateData, 6 FreePrivateData, 7 SetPriority, 8 GetPriority,
                //   9 PreLoad, 10 IsDirty, 11 GetDesc ← 推导值，已加 InD3d9 守卫
                SurfGetDesc_t gd = (SurfGetDesc_t)svt[VTI_SURF_GETDESC];
                if (gd && InD3d9(gd)) gd(s, &sd);
                if (sd.w && sd.h) { g_vpW = sd.w; g_vpH = sd.h; }
                void** r2 = svt;
                typedef long (__stdcall *Rel_t)(void*);
                Rel_t rel = (Rel_t)r2[2];
                if (InD3d9(rel)) rel(s);
            }
        }
    }

    GetTransform_t gxf = (GetTransform_t)dvt[VTI_GETTRANSFORM];
    if (gxf && InD3d9(gxf)) {
        float W[16], V[16], P[16];
        ZeroBuf(W, sizeof(W)); ZeroBuf(V, sizeof(V)); ZeroBuf(P, sizeof(P));
        long hw = -1, hv = -1, hp = -1;
        __try {
            hw = gxf(dev, D3DTS_WORLD_, &W);
            hv = gxf(dev, D3DTS_VIEW_,  &V);
            hp = gxf(dev, D3DTS_PROJECTION_, &P);
        } __except (CjkTrap("IDirect3DDevice9::GetTransform", GetExceptionInformation())) { }

        if (hp == 0) {
            int isId = (P[0] == 1.0f && P[5] == 1.0f && P[10] == 1.0f && P[15] == 1.0f &&
                        P[12] == 0.0f && P[13] == 0.0f && P[14] == 0.0f);
            if (!isId) {
                float tmp[16];
                MatMul(tmp, W, V);
                MatMul(g_xf, tmp, P);
                g_liveConstX = g_xf[12];
                g_liveConstY = g_xf[13];
                g_liveScaleX = g_xf[0];
                g_liveScaleY = g_xf[5];
                g_mapMode = 2;
            }
        }
    }
    if (!g_mapMode) g_mapMode = 1;
    g_mapDev  = dev;
    g_mapTick = GetTickCount();
    // 注意：g_mapTick 只在真正抓取时更新（2ms 节流），别在这里无条件重置，
    //       否则节流失效、每块都抓。
}

static void CjkMap(float lx, float ly, int* sx, int* sy)
{
    // ini [layout] mode 覆盖：1=强制屏幕坐标（不加矩阵）2=强制矩阵
    int useMode = g_cfgMapMode ? g_cfgMapMode : g_mapMode;
    if (useMode == 2 && g_cfgMapMode != 2) useMode = g_mapMode;
    if (useMode == 1) {   // 屏幕坐标：不套矩阵
        *sx = (int)(lx + (float)g_vpW * 0.5f + (float)g_vpX + 0.5f) + g_cfgXoff;
        *sy = (int)(ly + (float)g_vpH * 0.5f + (float)g_vpY + 0.5f) + g_cfgYoff;
        return;
    }
    if (g_mapMode == 2) {
        float cx = g_xf[0] * lx + g_xf[4] * ly + g_xf[12];
        float cy = g_xf[1] * lx + g_xf[5] * ly + g_xf[13];
        float cw = g_xf[3] * lx + g_xf[7] * ly + g_xf[15];
        if (cw == 0.0f) cw = 1.0f;
        float nx = cx / cw, ny = cy / cw;
        *sx = (int)(( nx * 0.5f + 0.5f) * (float)g_vpW + (float)g_vpX + 0.5f) + g_cfgXoff;
        *sy = (int)((-ny * 0.5f + 0.5f) * (float)g_vpH + (float)g_vpY + 0.5f) + g_cfgYoff;
        return;
    }
    *sx = (int)(lx + (float)g_vpW * 0.5f + (float)g_vpX + 0.5f) + g_cfgXoff;
    *sy = (int)(ly + (float)g_vpH * 0.5f + (float)g_vpY + 0.5f) + g_cfgYoff;
}

static void CjkFlush()
{
    long n = g_qCount;
    if (n <= 0) return;
    if (n > CJKQ_MAX) n = CJKQ_MAX;

    void* dev = CjkActiveDevice();      // ★ 用引擎真正呈现的那个设备
    if (!dev || !LooksLikeDevice(dev)) { g_qCount = 0; return; }

    CjkUpdateMapping(dev);

    if (!g_mapDiagChk) {
        g_mapDiagChk = 1;
        char q[MAX_PATH];
        CjkSidePath("chs_mapdiag.txt", q, sizeof(q));
        g_mapDiag = (GetFileAttributesA(q) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
        LogPrintf("[mapdiag] enabled=%d\r\n", g_mapDiag);
        FlushFileBuffers(g_log);
    }

    for (long i = 0; i < n; ++i) {
        CjkItem* it = &g_q[i];

        int sx = it->x, sy = it->y;
        CjkMap((float)it->x, (float)it->y, &sx, &sy);

        // 诊断（chs_mapdiag.txt）：只在**矩阵平移项变化**时记一行，不再逐项刷屏
        if (g_mapDiag && ++g_mapDiagN <= 4000) {
            if (g_liveConstX != g_mapDiagLastX || g_liveConstY != g_mapDiagLastY) {
                g_mapDiagLastX = g_liveConstX; g_mapDiagLastY = g_liveConstY;
                LogPrintf("[mapdiag] const changed -> c=(%.5f,%.5f) s=(%.5f,%.5f) mode=%d "
                          "| logic=(%d,%d) -> screen=(%d,%d)\r\n",
                          (double)g_liveConstX, (double)g_liveConstY,
                          (double)g_liveScaleX, (double)g_liveScaleY, g_mapMode,
                          it->x, it->y, sx, sy);
                FlushFileBuffers(g_log);
            }
        }

        int top = sy;
        if (it->align & 8)      top -= (it->nl * it->h) / 2;   // 垂直居中
        else if (it->align & 4) top -= (it->nl * it->h);       // 底部对齐

        int pos = 0;
        int usedManual = 0;
        if (!g_manFatal && g_manFail < 8) {
            for (int L = 0; L < it->nl && pos < CJKQ_CHARS; ++L) {
                ManDrawLine(dev, it->t + pos, sx, top + L * it->h, it->h, it->align & 3);
                // ★ 推进必须**无条件**停在 CJKQ_CHARS 内。
                //   旧写法 `while (it->t[pos+k]) ++k;` 在整段无 NUL 时会一路冲出去，
                //   第 9 轮实测就崩在下一轮 `it->t + pos` 的读取上。
                int k = 0;
                while (pos + k < CJKQ_CHARS - 1 && it->t[pos + k]) ++k;
                if (pos + k >= CJKQ_CHARS - 1) { pos = CJKQ_CHARS; break; }  // 无终止符，停
                pos += k + 1;   // 跳过这一行的 NUL
            }
            usedManual = 1;
        }

        if (!usedManual) {
            // ---- 兜底：回到 D3DX 字体（实测恒失败，只是留条后路）----
            void* f = CjkFontFor(dev, it->h);
            if (!f) { g_cjkNoFont++; continue; }
            int top2 = it->y;
            if (it->align & 8)      top2 -= (it->nl * it->h) / 2;
            else if (it->align & 4) top2 -= (it->nl * it->h);
            int p2 = 0;
            for (int L = 0; L < it->nl && p2 < CJKQ_CHARS; ++L) {
                int w = 0;
                CjkMeasureW(f, it->t + p2, &w);
                int left = it->x;
                if (it->align & 1)      left -= w / 2;
                else if (it->align & 2) left -= w;
                CjkDrawLineW(f, it->t + p2, left, top2 + L * it->h);
                int k = 0;
                while (p2 + k < CJKQ_CHARS - 1 && it->t[p2 + k]) ++k;
                if (p2 + k >= CJKQ_CHARS - 1) break;   // 无终止符
                p2 += k + 1;
            }
        }
        g_cjkDrawn++;

        if (g_cjkDrawn <= 24 || (g_cjkDrawn % 500) == 0) {
            char txt[96];
            CjkWideToLog(it->t, txt, sizeof(txt));
            LogPrintf("[cjk #%lu] logic=(%d,%d) screen=(%d,%d) align=0x%X h=%d nl=%d "
                      "\"%s\" manHr=0x%08lX\r\n",
                      g_cjkDrawn, it->x, it->y, sx, sy,
                      (unsigned)it->align, it->h, it->nl,
                      txt, (unsigned long)g_manLastHr);
            FlushFileBuffers(g_log);
        }
    }
    g_qCount = 0;

    if (!g_mapLogged && g_cjkDrawn >= 1) {
        g_mapLogged = 1;
        LogPrintf("[map] mode=%d (%s) viewport=%lux%lu @(%lu,%lu)\r\n",
                  g_mapMode, g_mapMode == 2 ? "WORLD*VIEW*PROJ" : "center-origin",
                  g_vpW, g_vpH, g_vpX, g_vpY);
        LogPrintf("[map] manDraws=%lu manHr=0x%08lX fail=%d fatal=%d\r\n",
                  g_manDraws, (unsigned long)g_manLastHr, g_manFail, g_manFatal);
        if (g_mapMode == 2)
            LogPrintf("[map] xform: %.4f %.4f %.4f %.4f / %.4f %.4f %.4f %.4f\r\n",
                      (double)g_xf[0], (double)g_xf[4], (double)g_xf[8],  (double)g_xf[12],
                      (double)g_xf[1], (double)g_xf[5], (double)g_xf[9],  (double)g_xf[13]);
        FlushFileBuffers(g_log);
    }

    // 批次绘制现在由 ManDrawLine 在每识别出一项后立即触发（见那里的注释）
}

// 文本 → UTF-16。编码由 CjkSourceCP() 决定（默认 GBK，见其注释）。
static const WCHAR* CjkMakeWide(const char* text, int nbytes)
{
    if (!text || nbytes <= 0) return NULL;

    int cp = CjkSourceCP();
    int nw = MultiByteToWideChar(cp, 0, text, nbytes, NULL, 0);
    if (nw <= 0) return NULL;
    if (nw > CJKQ_CHARS - 1) nw = CJKQ_CHARS - 1;

    long slot = (InterlockedIncrement(&g_slot) - 1) & (CJK_SLOTS - 1);
    WCHAR* w = g_wideBuf[slot];
    int got = MultiByteToWideChar(cp, 0, text, nbytes, w, nw);
    if (got <= 0) return NULL;
    if (got > CJKQ_CHARS - 1) got = CJKQ_CHARS - 1;
    w[got] = 0;                               // 显式长度调用不会自动补终止符
    return w;
}

// 返回 1 = 已接管（调用者应直接 ret，不再走原位图流程）
// 返回 0 = 未接管，走原路径
extern "C" int __cdecl CjkDispatch(void* block)
{
    if (g_disabled) return 0;
    if (!g_disabledChk) CjkCheckKillSwitch();
    if (g_disabled) return 0;

    if (!block || IsBadReadPtr(block, 0x90)) { g_cjkBadPtr++; return 0; }

    char* text = (char*)SafeReadPtr((char*)block + 0x74, NULL);
    if (!text || IsBadReadPtr(text, 1)) { g_cjkBadPtr++; return 0; }

    int len = SafeReadInt((char*)block + 0x7C, 0);
    if (len <= 0 || len > 8192) len = 8192;

    // 实际长度：扫到 NUL 或上限（不信任 +0x7C）
    int n = 0;
    while (n < len) {
        if (IsBadReadPtr(text + n, 1)) break;
        if (!text[n]) break;
        ++n;
    }
    if (n == 0) return 0;

    // ---- 只有含 >=0x80 的字节才接管，其余一律放行给位图字体 ----
    int hasHi = 0;
    for (int i = 0; i < n; ++i) {
        if ((unsigned char)text[i] >= 0x80) { hasHi = 1; break; }
    }
    if (!hasHi) return 0;

    g_cjkCalls++;

    // ---- 拿 D3D 设备（拿不到就老老实实回退）----
    void* tr  = NULL;
    void* dev = CjkGetDevice(&tr);
    if (!dev) {
        dev = CjkGetDeviceFallback();      // 扫 screen 对象兜底（只做一次并留痕）
        if (dev && !g_device) g_device = dev;
    }
    if (!dev || !LooksLikeDevice(dev)) { g_cjkNoDev++; return 0; }

    void* font = SafeReadPtr((char*)block + 0x1C, NULL);
    if (!font || IsBadReadPtr(font, 0x390)) { g_cjkBadPtr++; return 0; }

    const WCHAR* src = CjkMakeWide(text, n);
    if (!src) { g_cjkConvFail++; return 0; }

    // ---- 几何：只取"锚点"，不抄引擎的对齐算式 ----
    //
    //   sub_426680 的对齐（426775-426853）配合字形绘制是这么对齐的：
    //     默认：      x = posX + ox(ox = cw/2)，字形以 x 为中心 → 左边缘 = posX
    //     居中(bit0)：x = posX - (n-1)*cw/2 → 整行宽度 n*cw 的**中心 = posX**
    //     右对齐(bit1)：右边缘 = posX
    //   结论：**posX / posY 本身就是锚点** ——
    //     默认=左上角、居中=中心、右对齐=右边缘；Y 轴同理（oy = lh/2）。
    //
    //   ★ 但必须自己重算宽度：n 是**字节数**（逐字节推进 charWidth），
    //     汉字 2 字节 1 个字形，而我们的字形宽只有 fontH，不是 2*cw。
    //     照抄 linesH 会把整段文字推进屏幕外（第四轮实测 x = -286）。
    //   所以这里只存锚点 + 对齐位，宽度留到 CjkFlush 用 DT_CALCRECT 实测。
    int   flags  = SafeReadInt((char*)block + 0x80, 0);
    int   linesH = SafeReadInt((char*)block + 0x88, 0);
    int   linesV = SafeReadInt((char*)block + 0x84, 0);
    float px = SafeReadFloat((char*)block + 0x30, 0.0f);
    float py = SafeReadFloat((char*)block + 0x34, 0.0f);
    float cw = SafeReadFloat((char*)font + 0x1C, 0.0f);
    float lh = SafeReadFloat((char*)font + 0x20, 0.0f);

    // ---- 字号：cw / lh **就是屏幕像素**，不要再乘任何缩放 ----
    //   证据（sub_426680 逐条核对）：
    //     4269BD  movss xmm0, [ebx+1Ch] ; addss xmm0, [esi+30h] → 逐字推进 charWidth
    //     426882  movss xmm0, [ebx+20h] ; addss xmm0, [esi+34h] → 换行推进 lineHeight
    //   两处都没有乘系数，坐标直接进顶点。
    //   （sub_41B710 里那条 [TR+0x6C]/[TR+0x70] 缩放属于**死代码**通路，
    //    本作从不走 —— 第四轮按它算出的 sy=8.0 把字号顶到上限 72，错的。）
    int fontH = (int)(lh + 0.5f);
    if (fontH < 8)  fontH = 8;
    if (fontH > 72) fontH = 72;

    if (g_cjkCalls <= 6) {
        LogPrintf("[cjk] metrics: cw=%.2f lh=%.2f -> fontH=%d anchor=(%.1f,%.1f) "
                  "align=0x%X linesH=%d linesV=%d\r\n",
                  (double)cw, (double)lh, fontH,
                  (double)px, (double)py, (unsigned)(flags & 0xF), linesH, linesV);
        FlushFileBuffers(g_log);
    }

    // ---- 入队（队列满就先刷一次，绝不越界）----
    long idx = g_qCount;
    if (idx >= CJKQ_MAX) { CjkFlush(); idx = g_qCount; }
    if (idx >= CJKQ_MAX) return 0;

    CjkItem* it = &g_q[idx];
    int o  = 0;
    int nl = 1;
    for (int i = 0; i < CJK_BUFSZ && src[i]; ++i) {
        WCHAR c = src[i];
        if (c == 0x1B) {                       // ESC + 颜色码：位图通路里是
            if (src[i + 1]) ++i;               //   "切换颜色、不绘制"，直接吃掉
            continue;
        }
        if (c == 0x0D) continue;
        if (o >= CJKQ_CHARS - 2) break;
        if (c == 0x0A) {
            it->t[o++] = 0;                    // 行之间用内嵌 NUL 分隔
            if (nl < 32) ++nl;
        } else if (c == 0x09) {
            it->t[o++] = L' ';
        } else {
            it->t[o++] = c;
        }
    }
    it->t[o] = 0;
    it->x     = (int)(px + 0.5f);
    it->y     = (int)(py + 0.5f);
    it->h     = fontH;
    it->nl    = nl;
    it->align = flags & 0xF;

    InterlockedIncrement(&g_qCount);

    // 入队后立刻处理（raster + emit + 重画整批）
    if (g_qCount >= CJKQ_MAX) { CjkFlush(); idx = g_qCount; if (idx >= CJKQ_MAX) return 0; }
    CjkFlush();

    return 1;
}

// ================================================================ 裸汇编 hook 入口
// 栈纪律完全手工控制：
//   进入时 [esp] = 返回地址，ecx = Block
//   复现原函数 prologue（push ebp / mov ebp,esp / push -1）
//   然后 jmp 回 target+5，由原函数 epilogue 弹出返回地址 → 返回原调用方
//
// ★ 回跳偏移必须是 +5，不是 +8。
//   实测 RVA 0x26680 的真实字节（AlienShooter.exe v1.22DIC）：
//     0x26680  55              push ebp
//     0x26681  8B EC           mov  ebp, esp
//     0x26683  6A FF           push -1          ← imm8，两字节（不是 imm32 的 5 字节）
//     0x26685  68 68 47 4D 00  push 4D4768h     ← SEH handler
//     0x2668A  64 A1 00 00 00 00  mov eax, fs:[0]
//   所以复现完这三条指令后，jmp 目标是 target+5。
//   写 +8 会跳到 push 4D4768h 的中间，切断 SEH 安装 → 崩溃。

// 供裸汇编调用：返回宿主 exe 基址。
// 编译为 `mov eax, dword ptr [g_exe]; ret` —— 语义正确，
// 且不会被内联掉（调用点是裸汇编，编译器看不见）。
extern "C" BYTE* __cdecl CHS_GetExeBase()
{
    return (BYTE*)g_exe;
}

// 供裸汇编读取的宿主基址。
// ★ 注意：naked asm 里写 `mov eax, g_exe` 取到的是**变量本身的地址**（mov reg,mem
//   语义），不是 g_exe 存的值 —— 那会跳到完全无关的地址上，进程直接崩。
//   正确做法是通过一个返回值的函数读它，编译器会生成
//   `mov eax, [g_exe]`，语义正确。
extern "C" __declspec(naked) void __cdecl CHS_Hook()
{
    __asm {
        // ① 探测统计（纯观测，不改任何行为）
        push ecx                       // ① 保存 this
        push ecx                       // ② 作为 ProbeBlock 的参数
        call ProbeBlock                // cdecl：调用者清参数
        add  esp, 4                    // 弹出参数
        pop  ecx                       // 恢复 this

        // ② 方向 A：含中文的文本块改走引擎自带 GDI 通路。
        //    返回 1 表示已经画完，必须跳过原位图流程（否则两套字形叠着画）。
        push ecx
        push ecx
        call CjkDispatch
        add  esp, 4
        pop  ecx
        test eax, eax
        jnz  short chs_handled

        // ③ 未接管 —— trampoline 回原函数
        // ---- 复现原函数前三条指令（共 5 字节）----
        push ebp
        mov  ebp, esp
        push 0FFFFFFFFh               // MSVC 编码为 6A FF（imm8），与原函数一致
        // ---- 跳回原函数 target + 5 ----
        call CHS_GetExeBase           // eax = exe 基址（内部读 [g_exe]）
        add  eax, 0x26680 + 5
        jmp  eax

    chs_handled:
        // 此刻 ESP 已回到刚进来的位置（栈顶就是返回地址），直接 ret 即可。
        // 不能 push ebp / 建 SEH —— 那是原函数自己的事，这里根本没进去。
        retn
    }
}

// 供 dllmain 调用：只打开日志，不装 hook。
// 存在的意义是让"被加载"与"hook 装上"两件事分别有独立证据 ——
// 否则 hook 安装失败时，连"DLL 到底有没有被加载"都无法判断。
extern "C" void __cdecl CHS_LogBoot(HMODULE hExe)
{
    if (!g_exe) g_exe = hExe;
    LogOpen();
}

// ================================================================ 跳板安装

static void InstallHook()
{
    LogOpen();

    if (!g_exe) { LogPrintf("[install] g_exe is NULL, abort\n"); return; }

    // ★ 必须在游戏调用 Direct3DCreate9 之前装好。
    //   ASI loader 在进程初始化阶段就加载本 DLL，早于游戏 WinMain —— 来得及。
    InstallD3DCapture();

    BYTE* target = (BYTE*)g_exe + RVA_DrawText;

    char info[256];
    my_snprintf(info, sizeof(info),
        "[install] base  = %p\r\n[install] target= %p (RVA 0x%06X)\r\n",
        g_exe, target, RVA_DrawText);
    LogWrite(info);

    // 校验 prologue 字节 —— 移植/版本变化时宁可不动也不要乱跳。
    // 实测 v1.22DIC 的前 5 字节 = 55 8B EC 6A FF
    //   55        push ebp
    //   8B EC     mov  ebp, esp
    //   6A FF     push -1            ← imm8 编码，两字节
    // 早期版本误写成 6A 之前的 68（imm32），导致校验永远失败、hook 静默不装。
    static const BYTE kExpect[5] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };

    int diff = -1;
    for (int i = 0; i < 5; ++i) {
        if (target[i] != kExpect[i]) { diff = i; break; }
    }
    if (diff >= 0) {
        // 把实际字节全部打出来，否则日志只有一句 ABORT 无法定位
        char err[256];
        char got[64];
        int gi = 0;
        for (int i = 0; i < 16 && gi < 60; ++i)
            gi += my_snprintf(got + gi, sizeof(got) - gi, "%02X ", target[i]);
        my_snprintf(err, sizeof(err),
            "[install] !! prologue mismatch at byte %d\r\n"
            "[install] expected: 55 8B EC 6A FF\r\n"
            "[install] actual  : %s\r\n"
            "[install] ABORT (未写入任何字节)\r\n",
            diff, got);
        LogWrite(err);
        return;
    }

    DWORD oldProt = 0;
    if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &oldProt)) {
        char err[128];
        my_snprintf(err, sizeof(err),
            "[install] VirtualProtect failed err=%lu\r\n", GetLastError());
        LogWrite(err);
        return;
    }

    // 手写字节拷贝：避免编译器为定长拷贝生成内建 memcpy/memset 引用
    // （本 DLL 不链接 CRT，那些符号无处可寻）。
    BYTE* t = target;
    BYTE  patch[5];
    patch[0] = 0xE9;
    DWORD rel = (DWORD)((BYTE*)CHS_Hook - (target + 5));
    patch[1] = (BYTE)(rel        );
    patch[2] = (BYTE)(rel >>  8  );
    patch[3] = (BYTE)(rel >> 16  );
    patch[4] = (BYTE)(rel >> 24  );
    t[0] = patch[0]; t[1] = patch[1]; t[2] = patch[2]; t[3] = patch[3]; t[4] = patch[4];

    DWORD junk = 0;
    VirtualProtect(target, 5, oldProt, &junk);
    FlushInstructionCache(GetCurrentProcess(), target, 5);

    // 写后读回校验：确认 5 字节真的落地（页面可写失败、AV 拦截等情况下
    // VirtualProtect 返回 TRUE 但写入被吞掉的情况并非没有）。
    char ok[224];
    int bad = -1;
    for (int i = 0; i < 5; ++i) if (target[i] != patch[i]) { bad = i; break; }

    if (bad >= 0) {
        my_snprintf(ok, sizeof(ok),
            "[install] !! write-back verify FAILED at byte %d (target=%p)\r\n"
            "[install] ABORT\r\n\r\n", bad, (void*)target);
        LogWrite(ok);
        return;
    }

    my_snprintf(ok, sizeof(ok),
        "[install] E9 written rel=0x%08lX\r\n"
        "[install] hook addr = %p\r\n"
        "[install] write-back verified OK\r\n"
        "[install] hook armed\r\n\r\n",
        rel, (void*)CHS_Hook);
    LogWrite(ok);

    // ---- 方向 B 设施自检 ----
    // DllMain 阶段 D3D 设备还没建起来，这里必然是 0 —— 记录一次即可，
    // 真正的判断发生在第一次要画中文时（见 CjkDispatch）。
    {
        void* tr  = NULL;
        void* dev = CjkGetDevice(&tr);
        char fl[256];
        my_snprintf(fl, sizeof(fl),
            "[facility] text-renderer([screen+0xE34]) = %p  (本作恒为 0，勿依赖)\r\n"
            "[facility] device via IAT hook           = %p  -> %s\r\n"
            "[facility] d3dx9_43 / d3d9 modules       = %p / %p\r\n\r\n",
            tr, dev,
            dev ? "captured" : "not yet (等待 Direct3DCreate9 / CreateDevice)",
            GetModuleHandleA("d3dx9_43.dll"), GetModuleHandleA("d3d9.dll"));
        LogWrite(fl);
    }
}

// ================================================================ 导出入口

extern "C" __declspec(dllexport) void __cdecl CHS_SetExeBase(HMODULE h)
{
    g_exe = h;
}

extern "C" __declspec(dllexport) void __cdecl CHS_Init()
{
    InstallHook();
}

extern "C" __declspec(dllexport) void __cdecl CHS_SetTextDump(int on)
{
    g_dumpText = on;
}

extern "C" __declspec(dllexport) void __cdecl CHS_Dump();

// ---- 自动 dump ----
// 没有任何外部调用者会调 CHS_Dump（游戏不知道这个导出存在），
// 所以按调用次数阈值自动落盘：菜单/关卡切换时会反复触发同一个
// 文本绘制函数，攒到阈值就把当前统计写进日志。
static unsigned long g_lastAutoDump = 0;

static void AutoDumpIfDue()
{
    // 探测阶段用 500（怕没数据就崩）；现在关键状态都有独立日志行，
    // 放宽到 2000，免得日志被直方图刷爆。
    const unsigned long kInterval = 2000;
    if (g_callCount < kInterval) return;
    if (g_callCount - g_lastAutoDump < kInterval) return;
    g_lastAutoDump = g_callCount;

    unsigned long printable = 0;
    for (int i = 0x20; i < 256; ++i) printable += g_byteHist[i];

    LogPrintf("[auto-dump @ call=%lu]\r\n", g_callCount);
    FlushFileBuffers(g_log);   // 崩了也要留住这一段
    LogPrintf("printable=%lu ctrl=%lu esc=%lu nl=%lu tab=%lu fontTag=%lu gdiPath=%lu badBlock=%lu\r\n",
              printable, g_ctrlCount, g_escCount, g_newlineCount,
              g_tabCount, g_fontTagCount, g_gdiPathCount, g_badBlockCount);
    LogPrintf("cjk: calls=%lu drawn=%lu noDev=%lu noFont=%lu convFail=%lu badPtr=%lu badVtbl=%lu trapped=%lu\r\n",
              g_cjkCalls, g_cjkDrawn, g_cjkNoDev, g_cjkNoFont, g_cjkConvFail, g_cjkBadPtr,
              g_cjkBadVtbl, g_cjkTrapped);
    LogPrintf("d3d: dev=%p endScene=%lu present=%lu reset=%lu fontHr=0x%08lX h=%d drawHr=0x%08lX\r\n",
              g_device, g_endSceneCalls, g_presentCalls, g_resetCalls,
              (unsigned long)g_fontHr, g_fontHeight, (unsigned long)g_lastDrawHr);
    LogPrintf("frame: begin=%lu clear=%lu setVp=%lu | present(dev/sw)=%lu/%lu | "
              "batch=%lu pending=%d\r\n",
              g_beginSceneCalls, g_clearCalls, g_setVpCalls,
              g_presentCalls, g_swapPresentCalls, g_batchDrawn, g_nQuads);
    LogPrintf("frame: newFrame gap/sig=%lu/%lu | gapHist <=1=%lu <=3=%lu <=8=%lu "
              "<=16=%lu >16=%lu | lastGap=%lu\r\n",
              g_frameNewByGap, g_frameNewBySig,
              g_gapHist[0], g_gapHist[1], g_gapHist[2], g_gapHist[3], g_gapHist[4],
              g_lastGap);
    LogPrintf("man: draws=%lu lastHr=0x%08lX fail=%d fatal=%d | sb=%lu/%lu apHr=0x%08lX | map=mode%d vp=%lux%lu@(%lu,%lu)\r\n",
              g_manDraws, (unsigned long)g_manLastHr, g_manFail, g_manFatal,
              g_sbCreateOk, g_sbCreateFail, (unsigned long)g_sbApplyHr,
              g_mapMode, g_vpW, g_vpH, g_vpX, g_vpY);
    LogPrintf("charWidth=[%lu..%lu]\r\n", g_charWidthMin, g_charWidthMax);
    LogPrintf("align: ");
    for (int i = 0; i < 8; ++i) LogPrintf("[%d]=%lu ", i, g_alignHist[i]);
    LogPrintf("\r\nfontType(+0x70): ");
    for (int i = 0; i < 8; ++i) LogPrintf("[%d]=%lu ", i, g_fontTypeHist[i]);
    LogPrintf("\r\nbyte histogram (0x20+ only):\r\n");
    for (int i = 0x20; i < 256; ++i) {
        if (!g_byteHist[i]) continue;
        char lbl[2] = {0, 0};
        if (i < 0x7F) lbl[0] = (char)i;
        LogPrintf("  0x%02X %-3s %lu\r\n", i, lbl[0] ? lbl : "--", g_byteHist[i]);
    }
    LogPrintf("\r\n");
}

extern "C" __declspec(dllexport) void __cdecl CHS_Dump()
{
    unsigned long printable = 0;
    for (int i = 0x20; i < 256; ++i) printable += g_byteHist[i];

    LogPrintf("\r\n======== dump @ call=%lu ========\r\n", g_callCount);
    LogPrintf("printable=%lu ctrl=%lu esc=%lu nl=%lu tab=%lu fontTag=%lu gdiPath=%lu\r\n",
              printable, g_ctrlCount, g_escCount, g_newlineCount,
              g_tabCount, g_fontTagCount, g_gdiPathCount);
    LogPrintf("charWidth=[%lu..%lu]\r\n", g_charWidthMin, g_charWidthMax);
    LogPrintf("align(af&0x70)>>4: ");
    for (int i = 0; i < 8; ++i) LogPrintf("[%d]=%lu ", i, g_alignHist[i]);
    LogPrintf("\r\nfontType(+0x70): ");
    for (int i = 0; i < 8; ++i) LogPrintf("[%d]=%lu ", i, g_fontTypeHist[i]);
    LogPrintf("\r\n\r\nbyte histogram (0x20+ only):\r\n");
    for (int i = 0x20; i < 256; ++i) {
        if (!g_byteHist[i]) continue;
        char lbl[2] = {0, 0};
        if (i < 0x7F) lbl[0] = (char)i;
        LogPrintf("  0x%02X %-3s %lu\r\n", i, lbl[0] ? lbl : "--", g_byteHist[i]);
    }
    LogPrintf("\r\n");
}
