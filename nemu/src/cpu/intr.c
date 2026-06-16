// intr.c - 中断和异常处理（PA3 核心文件）
// 功能：实现中断/异常的触发机制，模拟 CPU 如何响应中断
// 这是操作系统运行的基础：系统调用、设备中断、异常处理都依赖这个机制

#include "cpu/exec.h"     // 指令执行框架
#include "memory/mmu.h"   // MMU 相关定义（包含 GateDesc 门描述符结构）

// raise_intr: 触发中断/异常（PA3 的核心函数）
// 参数：NO = 中断/异常号（0-255），ret_addr = 返回地址
// 功能：模拟 CPU 响应中断的硬件行为
// 工作流程：
//   1. 保存现场：压栈 EFLAGS、CS、EIP
//   2. 从 IDT（中断描述符表）读取中断处理程序地址
//   3. 跳转到中断处理程序
void raise_intr(uint8_t NO, vaddr_t ret_addr) {
  /* TODO: Trigger an interrupt/exception with ``NO''.
   * That is, use ``NO'' to index the IDT.
   */
  // 触发中断/异常的标准流程（x86 硬件自动完成的操作）

  // ──────── 第 1 步：保存现场（压栈 3 个值）────────
  rtl_li(&t0, cpu.eflags.val);          // t0 = EFLAGS 寄存器的值
  rtl_push(&t0);                        // 压栈 EFLAGS（保存标志位状态）
                                        // 这样中断处理完后可以恢复原来的标志位

  rtl_li(&t0, cpu.cs ? cpu.cs : 0x8);   // t0 = CS 寄存器的值
                                        // 如果 CS 为 0，默认使用 0x8（内核代码段选择子）
  rtl_push(&t0);                        // 压栈 CS（保存代码段）
                                        // 这样可以支持跨特权级的中断（用户态→内核态）

  rtl_li(&t0, ret_addr);                // t0 = 返回地址
                                        // 对于异常：ret_addr = 触发异常的指令地址（重新执行该指令）
                                        // 对于中断：ret_addr = 被中断指令的下一条指令（继续执行）
  rtl_push(&t0);                        // 压栈 EIP（保存返回地址）

  cpu.eflags.IF = 0;                    // 清除中断标志（禁止嵌套中断）
                                        // 防止在处理中断时又被其他中断打断
                                        // 如果中断处理程序想允许嵌套中断，可以执行 sti 指令

  // ──────── 第 2 步：从 IDT 读取中断处理程序地址 ────────
  GateDesc desc;                        // 门描述符（Gate Descriptor）
                                        // IDT 的每一项是一个门描述符，描述中断处理程序的入口
  desc.val = vaddr_read(cpu.idtr.base + NO * 8, 4);
  // 读取门描述符的低 4 字节
  // IDT 基址 = cpu.idtr.base（由 lidt 指令设置）
  // 门描述符地址 = IDT 基址 + 中断号 * 8（每个门描述符 8 字节）

  *(((uint32_t *)&desc) + 1) = vaddr_read(cpu.idtr.base + NO * 8 + 4, 4);
  // 读取门描述符的高 4 字节
  // 这样 desc 就包含了完整的 8 字节门描述符

  decoding.jmp_eip = (desc.offset_31_16 << 16) | desc.offset_15_0;
  // 从门描述符中提取中断处理程序的入口地址
  // 门描述符的结构：
  //   offset_15_0：入口地址的低 16 位
  //   offset_31_16：入口地址的高 16 位
  // 拼接成完整的 32 位地址

  decoding.is_jmp = 1;                  // 标记为跳转（exec_wrapper 会更新 eip）

  // ──────── 跳转到中断处理程序 ────────
  // exec_wrapper 会检查 decoding.is_jmp，然后执行：
  //   eip = decoding.jmp_eip
  // 这样 CPU 就跳转到中断处理程序开始执行
}

// dev_raise_intr: 设备触发中断（由设备驱动调用）
// 功能：设置中断请求标志，通知 CPU 有外部中断待处理
// 工作原理：
//   1. 设备（定时器、键盘等）调用这个函数
//   2. cpu.INTR 被设置为 true
//   3. CPU 在 exec_wrapper 的 poll_intr 中检查 cpu.INTR
//   4. 如果 cpu.INTR=true 且 IF=1，触发 32 号中断（时钟中断）
void dev_raise_intr() {
  cpu.INTR = true;                      // 设置中断请求标志
                                        // cpu.INTR = Interrupt Request（中断请求）
}

// ──────────────────────────────────────────────────────────────────────────────
// 中断分类和中断号分配：
//
// x86 架构支持 256 个中断/异常（0-255），分为三类：
//
// 1. 异常（Exception）：0-31 号
//    由 CPU 内部事件触发，例如：
//      0  = 除零异常（Divide Error）
//      6  = 非法指令异常（Invalid Opcode）
//      13 = 通用保护异常（General Protection Fault）
//      14 = 缺页异常（Page Fault）
//
// 2. 外部中断（Hardware Interrupt）：32-255 号
//    由外部设备触发，例如：
//      32 = 时钟中断（Timer，IRQ 0）
//      33 = 键盘中断（Keyboard，IRQ 1）
//
// 3. 软中断（Software Interrupt）：可以是任意号
//    由 int 指令触发，例如：
//      0x80 = Linux 系统调用（int 0x80）
//      3    = 调试断点（int 3）
//
// ──────────────────────────────────────────────────────────────────────────────
// IDT（Interrupt Descriptor Table，中断描述符表）结构：
//
// IDT 是一个数组，每个元素是一个门描述符（Gate Descriptor），8 字节
//
// 门描述符结构（8 字节）：
//   字节 0-1：offset_15_0（入口地址的低 16 位）
//   字节 2-3：selector（目标代码段选择子，通常是内核代码段）
//   字节 4：  保留（0）
//   字节 5：  type、DPL 等属性位
//   字节 6-7：offset_31_16（入口地址的高 16 位）
//
// CPU 根据中断号索引 IDT，读取对应的门描述符，得到中断处理程序地址
//
// ──────────────────────────────────────────────────────────────────────────────
// 中断处理流程完整示例（以时钟中断为例）：
//
// 1. 定时器硬件每隔一段时间调用 dev_raise_intr()
//    → cpu.INTR = true
//
// 2. CPU 执行指令时，exec_wrapper 中的 poll_intr 检查到：
//    cpu.INTR=true 且 cpu.eflags.IF=1（中断允许）
//    → 调用 raise_intr(32, cpu.eip)
//
// 3. raise_intr 执行：
//    push EFLAGS（保存标志位）
//    push CS（保存代码段）
//    push EIP（保存返回地址）
//    IF = 0（禁止嵌套中断）
//    从 IDT[32] 读取时钟中断处理程序地址
//    跳转到时钟中断处理程序
//
// 4. 时钟中断处理程序（操作系统内核代码）：
//    pusha（保存所有寄存器）
//    处理时钟中断（更新系统时间、进程调度等）
//    向定时器芯片发送 EOI（End Of Interrupt，中断结束信号）
//    popa（恢复所有寄存器）
//    iret（中断返回）
//
// 5. iret 执行：
//    pop EIP（恢复返回地址）
//    pop CS（恢复代码段）
//    pop EFLAGS（恢复标志位，IF 恢复为 1）
//    继续执行被中断的程序
//
// ──────────────────────────────────────────────────────────────────────────────
// raise_intr 与真实 CPU 的对比：
//
// 真实 CPU：
//   - 中断响应是硬件电路自动完成的（极快，几个时钟周期）
//   - 硬件自动查找 IDT、压栈、跳转
//
// NEMU（模拟）：
//   - raise_intr 函数用软件模拟硬件行为（慢，要执行很多 C 代码）
//   - 但逻辑完全一样：压栈、查 IDT、跳转
//
// 这就是"模拟器"的本质：用软件重现硬件的行为
// ──────────────────────────────────────────────────────────────────────────────
