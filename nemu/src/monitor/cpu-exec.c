// cpu-exec.c - CPU 指令执行循环（NEMU 的心脏）
// 功能：模拟 CPU 的工作方式——不停地取指令、译码、执行
// 这是整个 NEMU 的核心，所有指令最终都通过这个循环执行

#include "nemu.h"             // NEMU 全局定义
#include "monitor/monitor.h"   // 监视器相关函数
#include "monitor/watchpoint.h"// 监视点检查函数

/* The assembly code of instructions executed is only output to the screen
 * when the number of instructions executed is less than this value.
 * This is useful when you use the `si' command.
 * You can modify this value as you want.
 */
// 只有执行的指令数量少于这个值时，才会打印汇编代码到屏幕
// 用途：使用 si 命令单步执行时，看到每条指令的汇编；但执行很多条时不打印（太多信息）
#define MAX_INSTR_TO_PRINT 10

#define IOE_UPDATE_INTERVAL 64
// IOE（I/O Extension）设备更新间隔：每执行 64 条指令，更新一次设备状态
// 避免每条指令都更新设备（太慢），但也不能间隔太久（设备响应延迟）

int nemu_state = NEMU_STOP;   // NEMU 的运行状态：NEMU_STOP（停止）、NEMU_RUNNING（运行中）、NEMU_END（程序结束）

void exec_wrapper(bool);      // 执行一条指令的包装函数（实际工作在 exec.c）

/* Simulate how the CPU works. */
// 模拟 CPU 的工作方式：这是 NEMU 的核心循环
// 真实的 CPU 就是这样工作的：不停地"取指令→译码→执行→更新PC"，永不停止
void cpu_exec(uint64_t n) {   // n = 要执行的指令条数
                              // ui.c 的 cmd_si 会调用这个函数
                              // 例如：si 5 → cpu_exec(5)
                              //       c → cpu_exec(-1)（-1 作为 uint64 是最大值）

  if (nemu_state == NEMU_END) {
    // 如果程序已经结束（执行到 HLT 指令或 nemu_trap），不能继续执行
    printf("Program execution has ended. To restart the program, exit NEMU and run again.\n");
    return;
  }
  nemu_state = NEMU_RUNNING;  // 标记 NEMU 进入运行状态

  bool print_flag = n < MAX_INSTR_TO_PRINT;
  // 如果要执行的指令数 < 10，打印每条指令的汇编（方便调试）
  // 否则不打印（执行成百上千条指令时，打印太多会很慢）

#ifdef HAS_IOE
  extern void device_update();  // 设备更新函数（键盘、定时器等）
  uint32_t ioe_interval = print_flag ? 1 : IOE_UPDATE_INTERVAL;
  // 如果要打印指令，每条指令都更新设备（单步调试时响应及时）
  // 否则每 64 条指令更新一次设备（批量执行时提高效率）
  uint32_t ioe_budget = ioe_interval;  // 设备更新预算（倒计时）
#endif

  // ★★★ 这是 NEMU 的核心循环：模拟 CPU 不停地执行指令 ★★★
  for (; n > 0; n --) {         // 循环 n 次
    /* Execute one instruction, including instruction fetch,
     * instruction decode, and the actual execution. */
    // 执行一条指令，包括三个阶段：
    //   1. 取指令（instruction fetch）：从 eip 指向的内存读取指令字节
    //   2. 译码（instruction decode）：解析指令的操作码和操作数
    //   3. 执行（execution）：调用对应的执行函数（例如 exec_add、exec_mov 等）
    exec_wrapper(print_flag);   // 执行一条指令（真正的工作在 exec.c 的 exec_wrapper）
                                // print_flag：是否打印这条指令的汇编代码

#ifdef DEBUG
    // 调试模式下，每执行一条指令都检查监视点
    if (check_watchpoints()) {  // 检查所有监视点，看是否有值发生变化
                                // check_watchpoints 在 watchpoint.c 实现
      Log("cpu_exec: stop by watchpoint at eip=0x%08x", cpu.eip);
      nemu_state = NEMU_STOP;   // 监视点触发，暂停执行
    }
#endif

#ifdef HAS_IOE
    // Device polling is much more expensive than a single guest instruction.
    // Keep single-step behavior unchanged, but batch updates for long runs.
    // 设备轮询比执行一条指令慢得多
    // 单步执行时每条指令都更新设备（保持响应），批量执行时每 64 条更新一次
    if (--ioe_budget == 0 || nemu_state != NEMU_RUNNING || n == 1) {
      // 三种情况需要更新设备：
      //   1. 预算用完（已经执行了 ioe_interval 条指令）
      //   2. NEMU 状态改变（例如遇到断点/监视点，要停下来）
      //   3. 这是最后一条指令（n == 1）
      device_update();          // 更新设备状态（键盘输入、定时器中断等）
      ioe_budget = ioe_interval;// 重置预算
    }
#endif

    if (nemu_state != NEMU_RUNNING) { return; }
    // 如果 NEMU 状态不再是"运行中"，立即退出循环
    // 可能的原因：
    //   - 遇到断点/监视点（nemu_state = NEMU_STOP）
    //   - 程序执行结束（nemu_state = NEMU_END）
    //   - 发生错误（例如除零、非法指令）
  }

  if (nemu_state == NEMU_RUNNING) { nemu_state = NEMU_STOP; }
  // 执行完 n 条指令后，如果还在运行状态，改成停止状态
  // 这样回到调试器提示符 "(nemu) "，等待用户输入下一条命令
}

// ──────────────────────────────────────────────────────────────────────────────
// CPU 执行循环工作流程图解：
//
// 用户敲命令 "si 5" 或 "c"
//         ↓
//   cpu_exec(5) 被调用
//         ↓
//   nemu_state = NEMU_RUNNING
//         ↓
//   ┌─────────────────────────────────┐
//   │  for (i = 0; i < 5; i++)        │  循环 5 次
//   │  {                              │
//   │    exec_wrapper(print_flag);    │  ← 执行一条指令（取指、译码、执行）
//   │    ↓                            │
//   │    check_watchpoints();         │  ← 检查监视点是否触发
//   │    ↓                            │
//   │    device_update();             │  ← 更新设备状态（键盘、定时器等）
//   │    ↓                            │
//   │    if (nemu_state != RUNNING)   │  ← 检查是否要停下来
//   │      break;                     │
//   │  }                              │
//   └─────────────────────────────────┘
//         ↓
//   nemu_state = NEMU_STOP
//         ↓
//   回到调试器提示符 "(nemu) "
//
// ──────────────────────────────────────────────────────────────────────────────
// exec_wrapper 的工作（在 exec.c）：
//
// 1. 取指令（instruction fetch）：
//    opcode = vaddr_read(cpu.eip, 1);  // 从 eip 读取 1 字节操作码
//
// 2. 译码（instruction decode）：
//    根据 opcode 在 opcode_table 中查找对应的译码函数和执行函数
//    译码函数解析操作数（寄存器、内存地址、立即数等）
//
// 3. 执行（execution）：
//    调用执行函数（例如 exec_add、exec_mov 等）
//    执行函数会：
//      - 读取操作数的值
//      - 执行运算
//      - 写回结果
//      - 更新 EFLAGS
//
// 4. 更新 PC：
//    cpu.eip = decoding.is_jmp ? decoding.jmp_eip : decoding.seq_eip;
//    如果是跳转指令，eip = 目标地址；否则 eip = 下一条指令地址
//
// ──────────────────────────────────────────────────────────────────────────────
// 真实 CPU 的工作方式：
//
// 真实的 CPU 硬件内部也是一个类似的循环：
//
// while (1) {
//   指令 = 从内存[PC]取指令;
//   PC += 指令长度;
//   译码(指令);
//   执行(指令);
//   if (跳转) PC = 目标地址;
// }
//
// 区别：
//   - 真实 CPU 用硬件电路实现（极快，GHz 频率）
//   - NEMU 用软件模拟（慢，每条指令要执行很多 C 代码）
//   - 但工作原理是一样的！这就是"存储程序"计算机的本质
//
// ──────────────────────────────────────────────────────────────────────────────
