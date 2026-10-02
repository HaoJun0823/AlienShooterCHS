// block_layout.h —— Alien Shooter 内部结构与地址定义
// 所有偏移/地址均来自 IDA 反汇编逐指令核对（AlienShooter.exe, ImageBase 0x400000）
//
// ★ 关键：exe 开启了 ASLR（DYNAMIC_BASE=1），绝对地址不可硬编码。
//   所有 RVA 在运行时用 g_exe + RVA 计算。

#pragma once

// ---------------------------------------------------------------- 目标函数 RVA
#define RVA_DrawText        0x00026680u   // sub_426680 文本绘制主循环（替换目标）
// ★ 名字有历史包袱：它**不是** GDI 绘制，是 D3D9 位图字形四边形渲染入口。
//   真实签名：sub_4306A0(screen, float x, float y, int color, const char* text)
//   （早前误判为 (x, y, text, n)，方向 A 据此把 -1 当 text 传了进去）
#define RVA_GdiDrawText     0x000306A0u   // sub_4306A0 —— 死代码，见 OFF_TextRenderer 注释
#define RVA_FindGlyph       0x00017F80u   // sub_417F80 字形查找
#define RVA_HasGlyph        0x00017F50u   // sub_417F50 字形存在判定
#define RVA_FontMgrSlot     0x00122C20u   // dword_522C20 全局字体/脚本上下文
#define RVA_NullGlyph       0x0024C4F0u   // dword_64C4F0 字形查找失败回退对象
#define RVA_FontLoad        0x0004A6A0u   // sub_44A6A0 PICTURE_FONT::vtbl[3]，内含 capacity=256

// Tab 跳格宽度系数（dword_4D99E0，值 7.0f）
#define RVA_TabScale        0x000D99E0u

// ---- 屏幕/渲染器单例 ----
// dword_502AD4：屏幕单例指针
#define RVA_GlobalScreen    0x00102AD4u

// ---- 导入表槽：Direct3DCreate9 ----
// 0x4D9314 = __imp_Direct3DCreate9（IDA imports_query 实测）
// 全 exe 唯一调用点 0x42DB4D（thunk 在 0x47C9C4）：
//     42DB4D  call Direct3DCreate9
//     42DB52  mov ecx, eax
//     42DB54  mov [edi+0E24h], ecx     ← edi = screen，IDirect3D9 存在 +0xE24
// 改写这个 IAT 槽就能在源头截获 D3D 对象（见 hook.cpp 的 D3D 捕获一节）
#define RVA_IAT_D3DCREATE9  0x000D9314u

// [screen + 0xE34]：★ 不是 GDI surface，是引擎的 D3D9 位图字形渲染器对象。
//   sub_4306A0 开头判空后把它当 this 交给 sub_41B710：
//     4306A6  cmp dword ptr [esi+0E34h], 0
//     4306D3  mov ecx, [esi+0E34h]
//     4306F4  call sub_41B710
#define OFF_TextRenderer    0x00000E34u

// ---- 字形渲染器对象（sub_41B710 的 this）内部偏移 ----
// 全部来自 sub_41B710 反汇编逐条核对（2026-10-03）
//
//   +0x5C  IDirect3DDevice9*     41B719  cmp [esi+5Ch],0 / 41B74E  SetFVF(0x144)
//                                41B771  SetStreamSource / 41B789  SetSamplerState
//                                41BB0B  DrawPrimitive(4, 0, ebx)
//   +0x64  IDirect3DVertexBuffer9*
//                                41B7C7  Lock(0,0,&p,0x2000)   ← vtable 0x2C
//                                41BAFD  Unlock()              ← vtable 0x30
//   +0x68  int     视口宽         41B863 / 41B86F  mulss（宽缩放分子）
//   +0x6C  int     视口高         41B883 / 41B8B0  mulss（高缩放分子）
//   +0x70  float   设计分辨率基准  41B899 divss / 41B8B4 divss（缩放分母）
//        → 屏幕像素 = 字形度量(图集纹素) * [+0x68 或 +0x6C] / [+0x70]
//   +0x74  字形度量表，每条 16 字节（left/top/right/bottom 四个 float），
//          按字符码索引： 41B845 [esi+16*ch+74h] / 41B83C [esi+16*ch+78h]
//                        41B836 [esi+16*ch+7Ch] / 41B85E [esi+16*ch+80h]
//          ★ 表长 = (0x1074 - 0x74) / 16 = 256 条 —— 位图字体，装不下 CJK
//   +0x1074 IDirect3DStateBlock9*  41B734 call [ecx+10h] → Capture()
//   +0x1078 IDirect3DStateBlock9*  41B740 call [ecx+14h] → Apply()
//
// ★★ 实机结论：+0x1074 / +0x1078 这两个状态块在本 exe 里从未被创建
//    （崩在 41B734，EAX=07E2831C 且 [EAX]=00000000 —— 虚表为空）。
//    整条 sub_4306A0 → sub_41B710 通路是死代码，且即便修好也只有 256 字形。
#define OFF_TR_Device       0x0000005Cu
#define OFF_TR_VertexBuf    0x00000064u
#define OFF_TR_ViewW        0x00000068u
#define OFF_TR_ViewH        0x0000006Cu
#define OFF_TR_DesignBase   0x00000070u
#define OFF_TR_GlyphTable   0x00000074u
#define OFF_TR_StateBlock   0x00001074u
// dword_4D9434：对齐计算用的系数，实测 = 0.5f（42677B movss xmm2, dword_4D9434）
#define RVA_HalfConst       0x000D9434u

// ---------------------------------------------------------------- Block 结构体
// esi = Block（`mov esi, ecx` @ 0x4266A8 → __thiscall，ecx 为 this）
// 偏移全部经 IDA 反汇编逐条核对（sub_426680）
//
// +0x00  int*    vtable
// +0x0C  int     curChar         绘制前写入当前字符码；字形绘制函数(vtbl[3])读取它
// +0x1C  void*   font            当前字体/字形对象，全程保存在 ebx
//                      ← 4266AA: mov ebx, [esi+1Ch]
// +0x30  float   posX            画笔 X
// +0x34  float   posY            画笔 Y
// +0x74  char*   text            ← 426740: mov eax, [esi+74h]
//                      ★ 是 char*，不是 char**。426748 直接 mov dl,[eax] 当字符串用。
//                      之前误写成 char** 再解一层 → 野指针 → 实机崩溃。
// +0x78  ---     lea eax,[esi+78h] → sub_425E40(&Block->field_78)，度量相关
// +0x7C  int     length          426870: cmp edi, [esi+7Ch]
// +0x80  int     alignFlags      4266D6: mov eax,[esi+80h]
//                      4266E1: and eax,70h  → bit4/5/6（0x10水平/0x20/0x40垂直）
//                      bit0/1 水平，bit2/3 垂直（426786 test dl,1 / 4267B0 test dl,2
//                      / 426811 test dl,8 / 426814 test dl,4）
// +0x84  int     linesV          4267EF: mov eax,[esi+84h]（垂直对齐用）
// +0x88  int     linesH          42678B: mov eax,[esi+88h]（水平对齐用）
//
// ---------------------------------------------------------------- 字形对象（font）
// +0x1C  float   charWidth       42679E: mulss xmm1, dword ptr [ecx+1Ch]
// +0x20  float   lineHeight      426802: mulss xmm1, dword ptr [ecx+20h]
// +0x70  int     type            4266C1: cmp dword ptr [ebx+70h], 7Eh
//                      ★ > 126 才走位图字体；<= 126 转 GDI（jle loc_426A05）
// +0x384 float   offsetX         4267CA: addss xmm1, dword ptr [ecx+384h]
// +0x388 float   offsetY         426836: addss xmm1, dword ptr [ecx+388h]
//
// ---------------------------------------------------------------- 字形查找（sub_417F80）
// int __thiscall sub_417F80(void* fontMgr, int ch)
//   上界字段 this+0x2B8，字形数组 this+0x2BC
//   失败返回 dword_64C4F0（0x64C4F0）
//
// ---------------------------------------------------------------- 主循环语义（0x426870 起）
//   循环条件：edi < Block->length 且 text[edi] != 0
//   0x0A \n   : posX = 行首X, posY += font->lineHeight
//   0x0D \r   : posX = 行首X
//   0x09 \t   : posX += font->charWidth * 7.0f
//   0x1B ESC  : 下一字节若在字体表内 → posX -= 该字形宽度（颜色切换，不绘制）
//   '<' + "<Font=" : 解析整数切换字体，跳过整段标签
//   < 0x20    : 跳过不绘制
//   >= 0x20   : Block->curChar = c; font->vtbl[3](font, Block);  然后 posX += charWidth
//
// ---------------------------------------------------------------- 入口早退（0x426A05）
//   font == NULL / font == 0x64C4F0 / font->type(+0x70) <= 126
//     → sub_4306A0(screen, float x, float y, int color, const char* text)
//   ★ 这条分支实测从未被走到（fontType 恒 > 126），且目标通路是死代码
//     （状态块未创建 + 字形表仅 256 条）。本项目不再使用它。
//
// ---------------------------------------------------------------- prologue 布局（用于 trampoline）
//   实测字节（AlienShooter.exe v1.22DIC, RVA 0x26680）：
//     0x26680  55                    push ebp
//     0x26681  8B EC                 mov  ebp, esp
//     0x26683  6A FF                 push -1                ← imm8，两字节
//     0x26685  68 68 47 4D 00        push 4D4768h           ← SEH handler
//     0x2668A  64 A1 00 00 00 00     mov  eax, fs:[0]
//     0x26690  50                    push eax
//     0x26691  83 EC 20              sub  esp, 20h
//
//   ★ trampoline 复现前 5 字节（55 8B EC 6A FF）后接回 target + 5。
//     MSVC 把 `push -1` 编码为 imm8 的 6A FF（两字节），不是 imm32 的
//     68 FF FF FF FF（五字节）。按 8 字节算会切在 `push 4D4768h` 中间，
//     SEH handler 装不上 → 崩溃。
//
// ---------------------------------------------------------------- hook 调用约定（致命坑）
//   0x4266A8  mov esi, ecx
//   → __thiscall：**ecx 就是 this**。hook 在入口接管，此时 ecx 持有 this。
//
//   ProbeBlock 声明为 __cdecl，**ecx 属于 caller-saved**，编译器可以随意改写。
//   早期版本裸汇编写作：
//       push ecx / call ProbeBlock / add esp,4 / <trampoline> / jmp
//   返回后 ecx 已是垃圾 → 原函数 `mov esi, ecx` 拿到野指针 →
//   `mov ebx,[esi+1Ch]` 立即访问违例 → 第一次绘制就崩。
//
//   正确写法（压两份，一份当参数、一份留着恢复）：
//       push ecx            ; 保存 this
//       push ecx            ; 参数
//       call ProbeBlock
//       add  esp, 4         ; 清参数
//       pop  ecx            ; 恢复 this
//       <trampoline>
//   实测字节：51 51 E8 <rel> 83 C4 04 59
//
//   同理：探针内不要用 x87 / 不要假设 xmm 存活。Win32 ABI 下
//   xmm0-xmm5 是 scratch（本作用 mulss 读 charWidth，属函数内使用，
//   在 hook 入口之后才发生，不受影响）；为保险，探针读浮点一律
//   按 IEEE754 位模式手算（BitsToLong），完全不碰 FPU/SSE。

// ---- 引擎帧末 Present 落点（2026-10-03 IDA 逐条核对）----
//   43041E  mov eax, [edi+0E28h]      ; edi = 引擎 D3D 包装对象(=dword_502AD4)
//   430424  lea edx, [ebp-14h]
//   43042C  mov ecx, [eax]            ; 设备虚表
//   430433  call dword ptr [ecx+44h]  ; Present（唯一 D3D Present 调用点）
// 0x43041E 那条指令正好 6 字节 ⇒ 可整条替换成 jmp(5) + nop
#define OFF_EngineDev        0x00000E28u   // [screen+0xE28] = 引擎实际使用/呈现的设备
#define RVA_FrameEndPatch    0x0003041Eu   // 被替换的 6 字节
#define RVA_FrameEndNext     0x00030424u   // 复原后跳回这里
