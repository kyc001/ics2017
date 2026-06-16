// system.c - 系统指令的执行函数（PA3/PA4 核心文件）
// 功能：实现 x86 的特权指令和系统相关指令
// 这些指令用于操作系统内核，涉及中断、I/O、控制寄存器等

#include "cpu/exec.h"         // 指令执行框架
#include "device/port-io.h"   // 端口 I/O 接口

void raise_intr(uint8_t NO, vaddr_t ret_addr);
// 声明：触发中断/异常的函数（实际实现在 intr.c）

// lidt 指令：加载中断描述符表寄存器（Load IDT Register）
// 语义：把内存中的 6 字节数据加载到 IDTR 寄存器
//       IDTR = {limit (2 字节), base (4 字节)}
// 用途：操作系统初始化时设置中断向量表的位置
// 例如：lidt [idt_ptr]
//       → IDTR.limit = [idt_ptr]（IDT 的长度 - 1）
//       → IDTR.base = [idt_ptr+2]（IDT 的起始地址）
make_EHelper(lidt) {
  cpu.idtr.limit = vaddr_read(id_dest->addr, 2);
  // 从内存读取 2 字节到 IDTR.limit（IDT 的长度 - 1）
  cpu.idtr.base = vaddr_read(id_dest->addr + 2, 4);
  // 从内存读取 4 字节到 IDTR.base（IDT 的起始地址）
  // IDT = Interrupt Descriptor Table（中断描述符表）
  // 存放了所有中断/异常的处理函数入口

  print_asm_template1(lidt);            // 打印汇编指令，例如："lidt 0x8048000"
}

// mov_r2cr 指令：寄存器 → 控制寄存器（Move Register to Control Register）
// 语义：cr = reg
// 用途：操作系统设置控制寄存器（特权指令，只能在内核态执行）
// 例如：mov %eax, %cr0（把 eax 的值写入 cr0，可能开启/关闭分页）
//       mov %eax, %cr3（把 eax 的值写入 cr3，切换页目录）
make_EHelper(mov_r2cr) {
  switch (id_dest->reg) {               // 根据目标寄存器编号选择
    case 0: cpu.cr0 = id_src->val; break;
    // CR0：控制寄存器 0，包含多个控制位
    //   bit 31 (PG)：分页使能（1=开启分页，0=关闭分页）
    //   bit 0 (PE)：保护模式使能（1=保护模式，0=实模式）
    case 3: cpu.cr3 = id_src->val; break;
    // CR3：页目录基址寄存器（Page Directory Base Register）
    //   存放页目录的物理地址（高 20 位，低 12 位为 0）
    //   切换 CR3 就是切换地址空间（进程切换时用）
    default: assert(0);                 // 其他控制寄存器（CR1/CR2/CR4）暂不支持
  }

  print_asm("movl %%%s,%%cr%d", reg_name(id_src->reg, 4), id_dest->reg);
  // 打印例如："movl %eax,%cr0"
}

// mov_cr2r 指令：控制寄存器 → 寄存器（Move Control Register to Register）
// 语义：reg = cr
// 用途：读取控制寄存器的值
make_EHelper(mov_cr2r) {
  switch (id_src->reg) {                // 根据源寄存器编号选择
    case 0: rtl_li(&t0, cpu.cr0); break;// t0 = CR0
    case 3: rtl_li(&t0, cpu.cr3); break;// t0 = CR3
    default: assert(0);
  }
  operand_write(id_dest, &t0);          // 把 t0 写到目标寄存器

  print_asm("movl %%cr%d,%%%s", id_src->reg, reg_name(id_dest->reg, 4));
  // 打印例如："movl %cr0,%eax"

#ifdef DIFF_TEST
  extern void diff_test_skip_qemu();
  diff_test_skip_qemu();                // 差分测试时跳过 QEMU 的对比
                                        // 因为 QEMU 的 CR 寄存器实现可能不同
#endif
}

// int 指令：软件中断（Software Interrupt）
// 语义：触发一个软中断，跳转到对应的中断处理程序
// 用途：系统调用（例如 Linux 的 int 0x80）、调试（int 3 断点）
// 工作流程：
//   1. 压栈：push EFLAGS; push CS; push EIP（保存现场）
//   2. 从 IDT 读取中断处理程序地址
//   3. 跳转到中断处理程序
make_EHelper(int) {
  raise_intr(id_dest->imm, decoding.seq_eip);
  // 触发中断：
  //   id_dest->imm = 中断号（例如 0x80）
  //   decoding.seq_eip = 返回地址（int 指令的下一条指令）

  print_asm("int %s", id_dest->str);    // 打印例如："int $0x80"

#ifdef DIFF_TEST
  extern void diff_test_skip_nemu();
  diff_test_skip_nemu();                // 差分测试时跳过 NEMU 的对比
                                        // 因为系统调用的行为难以在 NEMU 和 QEMU 间保持一致
#endif
}

// iret 指令：中断返回（Interrupt Return）
// 语义：从中断/异常返回
// 工作流程：
//   1. pop EIP（恢复返回地址）
//   2. pop CS（恢复代码段）
//   3. pop EFLAGS（恢复标志寄存器）
// 用途：中断处理程序的最后一条指令（与 int 配对）
make_EHelper(iret) {
  rtl_pop(&t0);                         // 弹出 EIP
  decoding.jmp_eip = t0;                // 设置跳转目标
  decoding.is_jmp = 1;                  // 标记为跳转
  rtl_pop(&t1);                         // 弹出 CS
  cpu.cs = t1;                          // 恢复代码段寄存器
  rtl_pop(&t2);                         // 弹出 EFLAGS
  cpu.eflags.val = t2;                  // 恢复标志寄存器

  print_asm("iret");                    // 打印："iret"
}

// in 指令：从 I/O 端口读取数据（Input from Port）
// 语义：dest = io_port[src]
// 用途：读取硬件设备的端口寄存器
// 例如：in $0x60, %al（从键盘控制器端口 0x60 读取 1 字节到 al）
//       in %dx, %eax（从 dx 指定的端口读取 4 字节到 eax）
make_EHelper(in) {
  uint32_t data = 0;
  switch (id_dest->width) {             // 根据目标操作数宽度选择读取函数
    case 1: data = pio_read_b(id_src->val); break;
    // pio_read_b = Port I/O Read Byte（读 1 字节）
    case 2: data = pio_read_w(id_src->val); break;
    // pio_read_w = Port I/O Read Word（读 2 字节）
    case 4: data = pio_read_l(id_src->val); break;
    // pio_read_l = Port I/O Read Long（读 4 字节）
    default: assert(0);
  }
  rtl_li(&t0, data);                    // t0 = 读取的数据
  operand_write(id_dest, &t0);          // 写到目标操作数

  print_asm_template2(in);              // 打印例如："in $0x60,%al"

#ifdef DIFF_TEST
  extern void diff_test_skip_qemu();
  diff_test_skip_qemu();                // 差分测试时跳过 QEMU 对比
                                        // 因为设备状态难以保持一致
#endif
}

// out 指令：向 I/O 端口写入数据（Output to Port）
// 语义：io_port[dest] = src
// 用途：写入硬件设备的端口寄存器
// 例如：out %al, $0x60（把 al 的值写到键盘控制器端口 0x60）
//       out %eax, %dx（把 eax 的值写到 dx 指定的端口）
make_EHelper(out) {
  switch (id_src->width) {              // 根据源操作数宽度选择写入函数
    case 1: pio_write_b(id_dest->val, id_src->val); break;
    // pio_write_b = Port I/O Write Byte（写 1 字节）
    case 2: pio_write_w(id_dest->val, id_src->val); break;
    // pio_write_w = Port I/O Write Word（写 2 字节）
    case 4: pio_write_l(id_dest->val, id_src->val); break;
    // pio_write_l = Port I/O Write Long（写 4 字节）
    default: assert(0);
  }

  print_asm_template2(out);             // 打印例如："out %al,$0x60"

#ifdef DIFF_TEST
  extern void diff_test_skip_qemu();
  diff_test_skip_qemu();                // 差分测试时跳过 QEMU 对比
#endif
}

// cli 指令：清除中断标志（Clear Interrupt Flag）
// 语义：IF = 0（禁止响应外部硬件中断）
// 用途：临界区保护（执行不可被中断的代码）
// 注意：cli 只禁止外部硬件中断，不影响异常和软中断（int 指令）
make_EHelper(cli) {
  rtl_li(&t0, 0);                       // t0 = 0
  rtl_set_IF(&t0);                      // IF = 0（禁止中断）
  print_asm("cli");                     // 打印："cli"
}

// sti 指令：设置中断标志（Set Interrupt Flag）
// 语义：IF = 1（允许响应外部硬件中断）
// 用途：退出临界区，重新允许中断
make_EHelper(sti) {
  rtl_li(&t0, 1);                       // t0 = 1
  rtl_set_IF(&t0);                      // IF = 1（允许中断）
  print_asm("sti");                     // 打印："sti"
}

// pushf 指令：压入标志寄存器（Push Flags）
// 语义：push EFLAGS
// 用途：保存当前的标志寄存器状态
make_EHelper(pushf) {
  rtl_li(&t0, cpu.eflags.val);          // t0 = EFLAGS 的值
  rtl_push(&t0);                        // 压栈
  print_asm("pushf");                   // 打印："pushf"
}

// popf 指令：弹出标志寄存器（Pop Flags）
// 语义：pop EFLAGS
// 用途：恢复之前保存的标志寄存器状态
make_EHelper(popf) {
  rtl_pop(&t0);                         // 从栈顶弹出值
  cpu.eflags.val = t0;                  // 恢复到 EFLAGS
  print_asm("popf");                    // 打印："popf"
}

// ──────────────────────────────────────────────────────────────────────────────
// 中断和异常处理流程：
//
// 1. 中断触发（硬件中断、软中断 int、异常）：
//    raise_intr(中断号, 返回地址) 被调用
//
// 2. CPU 自动保存现场（在 raise_intr 中实现）：
//    push EFLAGS（保存标志寄存器）
//    push CS（保存代码段）
//    push EIP（保存返回地址）
//
// 3. CPU 从 IDT 读取中断处理程序地址并跳转：
//    IDT_entry = IDT_base + 中断号 * 8（每个 IDT 项 8 字节）
//    EIP = IDT_entry.offset（跳转到中断处理程序）
//
// 4. 中断处理程序执行：
//    保存其他寄存器（pusha）
//    处理中断（例如系统调用、设备服务）
//    恢复寄存器（popa）
//
// 5. 中断返回：
//    iret 指令执行
//    pop EIP（恢复返回地址）
//    pop CS（恢复代码段）
//    pop EFLAGS（恢复标志寄存器）
//    继续执行被中断的程序
//
// ──────────────────────────────────────────────────────────────────────────────
// 系统调用流程（以 Linux int 0x80 为例）：
//
// 用户程序：
//   mov $1, %eax          // 系统调用号（1 = sys_exit）
//   mov $0, %ebx          // 参数 1（退出码 = 0）
//   int $0x80             // 触发系统调用
//
// ↓ int 0x80 执行：
//
// 1. 硬件自动：push EFLAGS; push CS; push EIP
// 2. 从 IDT[0x80] 读取系统调用处理程序地址
// 3. 跳转到系统调用处理程序（操作系统内核代码）
//
// 系统调用处理程序：
//   pusha                 // 保存所有寄存器
//   根据 eax（系统调用号）调用对应的内核函数
//   popa                  // 恢复所有寄存器
//   iret                  // 返回用户程序
//
// ↓ iret 执行：
//
// 1. pop EIP; pop CS; pop EFLAGS（恢复现场）
// 2. 继续执行 int $0x80 的下一条指令
// ──────────────────────────────────────────────────────────────────────────────
