// prefix.c - 指令前缀处理（PA2 核心文件）
// 功能：处理 x86 指令的前缀字节（改变后续指令的行为）
// x86 指令可以有多个前缀字节，出现在操作码之前

#include "cpu/exec.h"     // 指令执行框架

make_EHelper(real);       // 声明：真正的指令执行函数（在 exec.c 中定义）

// operand_size 前缀：0x66
// 功能：切换操作数大小（16位 ↔ 32位）
// 语义：临时改变操作数宽度，执行下一条指令后恢复
// 例如：在 32 位模式下，0x66 B8 1234 → mov $0x1234, %ax（16位）
//       正常情况下，B8 12345678 → mov $0x12345678, %eax（32位）
make_EHelper(operand_size) {
  decoding.is_operand_size_16 = true;   // 标记：当前指令使用 16 位操作数
                                        // 后续的译码和执行函数会检查这个标志
                                        // 例如：mov 指令会根据这个标志决定移动 2 字节还是 4 字节
  exec_real(eip);                       // 执行下一条指令（带有 16 位操作数）
                                        // exec_real 会继续取指令、译码、执行
  decoding.is_operand_size_16 = false;  // 恢复默认：32 位操作数
                                        // 前缀只影响紧跟的一条指令，不影响后续指令
}

// seg_override 前缀：0x26/0x2e/0x36/0x3e/0x64/0x65
// 功能：段覆盖前缀（Segment Override）和分支提示前缀
// 语义：改变默认的段寄存器选择
// 例如：0x2e 8B 00 → mov %cs:(%eax), %eax（使用 CS 段而不是默认的 DS 段）
//       0x64 8B 00 → mov %fs:(%eax), %eax（使用 FS 段）
make_EHelper(seg_override) {
  // In our flat-memory model, segment override and branch-hint style prefixes
  // such as 0x2e/0x3e can be ignored safely.
  // 在平坦内存模型中，段覆盖前缀可以安全地忽略
  // 原因：NEMU 使用平坦内存模型（所有段寄存器的基址都是 0，界限都是 4GB）
  //       所以无论选择哪个段，地址计算结果都一样
  // 真实的操作系统（如 Linux）也使用平坦内存模型，所以这个简化是合理的
  exec_real(eip);                       // 直接执行下一条指令，忽略段覆盖前缀
}

// rep 前缀：0xf3（也包括 0xf2 repne）
// 功能：重复执行字符串指令（Repeat）
// 语义：重复执行下一条指令，直到 ecx = 0
// 例如：rep movsb → while (ecx--) { movsb; }（复制 ecx 个字节）
//       rep stosb → while (ecx--) { stosb; }（填充 ecx 个字节）
make_EHelper(rep) {
  // 特殊处理：endbr32 指令（Intel CET 控制流保护）
  if (vaddr_read(*eip, 1) == 0x0f &&
      vaddr_read(*eip + 1, 1) == 0x1e &&
      vaddr_read(*eip + 2, 1) == 0xfb) {
    // 检查接下来的 3 字节是否为 0x0f 0x1e 0xfb（endbr32 指令）
    // endbr32 = End Branch 32（结束间接分支）
    // 这是 Intel CET（Control-flow Enforcement Technology）的一部分
    // 用于防止 ROP（Return-Oriented Programming）攻击
    instr_fetch(eip, 1);                // 跳过 0x0f
    instr_fetch(eip, 1);                // 跳过 0x1e
    instr_fetch(eip, 1);                // 跳过 0xfb
    // Log("pa2-debug: treat endbr32 as nop at 0x%08x", cpu.eip);
    print_asm("endbr32");               // 打印汇编指令："endbr32"
    return;                             // 把 endbr32 当作 nop 处理（什么都不做）
    // 注意：f3 0f 1e fb 也可能被误认为是 rep 前缀加其他指令
    //       所以要先检查 endbr32，避免错误解析
  }

  decoding.rep_prefix = true;           // 标记：当前指令有 rep 前缀
                                        // 字符串指令（movs、stos 等）会检查这个标志
                                        // 如果为 true，就循环执行 ecx 次
  exec_real(eip);                       // 执行下一条指令（带有 rep 前缀）
  decoding.rep_prefix = false;          // 恢复默认：无 rep 前缀
}

// ──────────────────────────────────────────────────────────────────────────────
// x86 指令前缀完整列表：
//
// 前缀类型           | 前缀字节        | 功能
// ───────────────────┼─────────────────┼──────────────────────────────────────
// 操作数大小前缀     | 0x66            | 切换 16/32 位操作数
// 地址大小前缀       | 0x67            | 切换 16/32 位地址（NEMU 不支持）
// 段覆盖前缀         | 0x26/0x2e/      | 选择 ES/CS/SS/DS/FS/GS 段
//                    | 0x36/0x3e/      | （平坦内存模型中忽略）
//                    | 0x64/0x65       |
// 重复前缀           | 0xf3 (rep)      | 重复 ecx 次（用于字符串指令）
//                    | 0xf2 (repne)    | 重复直到 ZF=1 或 ecx=0
// 锁前缀             | 0xf0 (lock)     | 原子操作（多核同步，NEMU 不支持）
//
// ──────────────────────────────────────────────────────────────────────────────
// 前缀的组合规则：
//
// 一条指令可以有多个前缀，按以下顺序出现：
//   [lock] [rep] [seg] [0x66] [0x67] opcode ...
//
// 例如：0x66 0xf3 0xa5 → rep movsw（重复复制 ecx 个字（2 字节））
//       0x2e 0x8b 0x00 → mov %cs:(%eax), %eax（从 CS 段读取）
//
// 无效的组合：
//   - lock 只能用于某些内存操作指令
//   - rep 只能用于字符串指令
//   - 多个同类前缀（例如 0x66 0x66）是未定义行为
//
// ──────────────────────────────────────────────────────────────────────────────
// rep 前缀的工作原理：
//
// 软件视角（C 语言）：
//   while (ecx > 0) {
//     执行字符串指令（例如 movsb）;
//     ecx--;
//   }
//
// 硬件视角（真实 CPU）：
//   - 字符串指令检查 rep 前缀标志
//   - 如果有 rep，就在微码层面循环执行
//   - ecx 每次递减，直到变成 0
//   - 中间可能被中断打断（保存 ecx，中断返回后继续）
//
// NEMU 的实现（在 data-mov.c）：
//   uint32_t count = decoding.rep_prefix ? cpu.ecx : 1;
//   while (count > 0) {
//     执行一次字符串操作;
//     count--;
//   }
//   if (decoding.rep_prefix) cpu.ecx = 0;
//
// ──────────────────────────────────────────────────────────────────────────────
// 平坦内存模型（Flat Memory Model）：
//
// 在实模式（16 位）下：
//   - 物理地址 = 段基址 * 16 + 偏移
//   - 不同的段寄存器 → 不同的段基址 → 不同的物理地址
//
// 在保护模式平坦内存模型下（32 位）：
//   - 所有段寄存器的基址都是 0，界限都是 4GB
//   - 物理地址 = 0 + 偏移 = 偏移
//   - 所以段选择不影响地址计算，可以忽略段覆盖前缀
//
// Linux 使用的就是平坦内存模型：
//   - 内核段和用户段的基址都是 0
//   - 只用 CS 区分内核态和用户态（通过特权级）
//   - 不用段来隔离进程（用分页机制隔离）
//
// ──────────────────────────────────────────────────────────────────────────────
// endbr32 指令（Intel CET）：
//
// 操作码：f3 0f 1e fb
// 功能：标记合法的间接跳转目标
// 用途：防止 ROP/JOP 攻击
//
// 原理：
//   - 开启 CET 后，间接跳转（jmp *reg、call *reg、ret）只能跳到 endbr 指令
//   - 如果跳到其他位置，触发 #CP 异常（Control Protection Exception）
//   - 这样攻击者就无法随意跳到代码中间（gadget）
//
// 为什么把它当作 nop：
//   - NEMU 不支持 CET，所以不检查间接跳转的合法性
//   - 现代编译器（gcc -fcf-protection）会在函数开头插入 endbr32
//   - 为了兼容这些二进制文件，NEMU 把 endbr32 当作 nop 忽略
//
// ──────────────────────────────────────────────────────────────────────────────
