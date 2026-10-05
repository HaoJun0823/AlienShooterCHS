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

// ---- 第 2 条文本绘制通路：sub_4306A0（本项目未接管，只挂统计探针）----
//   来源：sub_426680 只出现在 vtable 0x4da7c0 的 [4] 项；
//   而同一批 UI 对象的另一张 vtable 0x4da7e0 的 [6] 项 = 0x46D2B0，
//   0x46D2B0 / 0x46CCB0 / 0x422010 / 0x4279E0 / 0x428F30 / 0x42BE90 共 6 处都 call sub_4306A0。
//   sub_4306A0 反汇编（2026-10-04 核对，前 6 字节 = 55 8B EC 56 8B F1）：
//     4306A6  cmp dword ptr [esi+0E34h], 0     ; [screen+0xE34] == 0 ?
//     4306AD  jz short loc_4306F9              ; → 直接 retn 10h（什么都不画）
//     4306CE  movss xmm0, [ebp+0Ch]
//     4306D3  mov ecx, [esi+0E34h]             ; ← 字形渲染器
//     4306F4  call sub_41B710                  ; 位图字形渲染（表长 256，装不下 CJK）
//     ...
//     4306FB  retn 10h                         ; __thiscall + 4 个栈参数
//   签名（按 [ebp+8/0Ch/10h/14h] 的使用顺序推断）= (screen, float x, float y, int color, char* text)。
//
//   ★★ 2026-10-05 定论（IDA 逐条核对，推翻此前两条错误结论）★★
//   ① 它**不是**"非文本绘制" —— 早期只看到 `push dword [ebp+10h]` 就当成 int，
//      那**就是 text 指针**。真实签名 = __thiscall(screen, float x, float y,
//      char* text, int flag)，flag 实测恒为 -1。
//   ② 它**不是死代码** —— [screen+0xE34]（textRenderer）由 sub_430DD0 在
//      0x430E58 处 `mov [esi+0E34h], eax` 赋值（对象由 `new(0x107C)` 建出），
//      而 sub_430DD0 在 **0x42B1B5** 被真实调用，实参 = ("Courier", 7, 8)，
//      正对应 cfg 的 Font / FontSizeX / FontSizeY。实测 e34=0x08FB8528（非 0）。
//      之前读到 0，是探针记"首次调用"时 D3D 还没初始化完。
//   ③ 它与 sub_426680 **不是并列的两条独立通路**，而是 sub_426680 内部的
//      **路径 B**：分流点在 0x4266C1 `cmp [font+0x70],0x7E` → `jle 0x426A05`，
//      即 **font->type <= 126**（或 font 为空 / 等于 [0x64c4f0]）时走这里。
//   ⇒ 本项目从 2026-10-05 起**接管**它（hook.cpp 的 CHS_Hook4306A0），
//     含 >=0x80 的文本用 GDI+ 渲染器画，纯 ASCII 仍放行给 Courier 位图字形。
#define RVA_Sub4306A0       0x000306A0u   // sub_426680 的「路径 B」绘制入口（已接管）

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
// ★★ 状态块结论：+0x1074 / +0x1078 在本 exe 里从未被创建
//    （我们主动调 sub_41B710 时会崩在 41B734，EAX=07E2831C 且 [EAX]=00000000 —— 虚表为空）。
// ★★ 但 sub_4306A0 **绝非死代码**（2026-10-05 实测定论）：
//    ① 状态块崩溃来自"我们主动调用"，引擎自己调用时状态是它准备好的；
//    ② [screen+0xE34] 由 sub_430DD0 真实创建（new(0x107C)），sub_430DD0
//       在 0x42B1B5 被调用，实参 ("Courier", 7, 8)；实测 e34=0x08FB8528；
//    ③ 日志实测 sub_4306A0 被调用 14 次 ⇒ 路径 B 是活的。
//    ★ 字形表只有 256 条（位图字体，装不下 CJK）⇒ 这正是汉化版
//      "道具提示消失"的根因：提示文本查表得中文后交给它画，无字形 ⇒ 空白。
//    2026-10-05 起本项目接管它：含 >=0x80 的文本走 GDI+，ASCII 仍放行。
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
// ---------------------------------------------------------------- 入口早退（0x426A05）= 路径 B
//   触发条件（0x4266AD / 0x4266B5 / 0x4266C1 三处判断）：
//     font == NULL  或  font == [0x64C4F0]  或  font->type(+0x70) <= 126
//     → sub_4306A0(screen, float x, float y, const char* text, int flag=-1)
//       → sub_41B710(textRenderer, x, y, text, -1, 0)
//
//   ★★ 2026-10-05 推翻旧结论「这条分支实测从未被走到（fontType 恒 > 126）」★★
//     那个"恒 >126"只是**主菜单那几个块**的采样。日志实测 sub_4306A0 被调用
//     14 次 ⇒ 路径 B 是活的。而 textRenderer 的位图字形表只有 256 条，
//     画不出中文 ⇒ **汉化版道具提示消失**就发生在这里
//     （英文版查表得 ASCII，Courier 画得出；汉化版得中文，无字形 ⇒ 空白）。
//     2026-10-05 起本项目接管此路径，见 hook.cpp 的 CjkTakeoverPathB。
//
//   ★ 另一处关键机制（0x4266E1）：`flags & 0x70 == 0x60` = **动态文本块**。
//     引擎会用 [esi+0x78] 里的 key 查表（sub_426AF0 → sub_45D9B0），
//     在 0x426718 **覆盖** [esi+0x74]（也就是我们读的那个 text 指针）。
//     ⇒ 我们的 hook 在函数**开头**，此刻读到的还是空/旧值，会误判放行。
//       这正是"提示块明明有中文却进不了 [bx] 日志"的原因。
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
