// special.c - 特殊指令的执行函数（PA2 核心文件）
// 功能：实现一些特殊的伪指令和异常处理
// 包括：nop（空操作）、inv（非法指令）、nemu_trap（NEMU 特殊陷阱）

#include "cpu/exec.h"         // 指令执行框架
#include "monitor/monitor.h"   // 监视器状态定义

// nop 指令：空操作（No Operation）
// 语义：什么都不做，只是让 eip 前进（占用 1 字节）
// 用途：① 代码对齐（让某些指令对齐到 4/8/16 字节边界，提高缓存效率）
//       ② 占位符（为将来的代码留出空间）
//       ③ 调试时插入断点
// 注意：nop 虽然什么都不做，但仍然要花费 1 个时钟周期
make_EHelper(nop) {
  print_asm("nop");                     // 打印汇编指令："nop"
  // 没有任何操作，eip 已经在 exec_wrapper 中更新了
}

// inv 指令：非法指令（Invalid Opcode）
// 这不是一个真实的 x86 指令，而是 NEMU 的特殊处理
// 当遇到未实现的指令或错误的指令字节时，会执行这个函数
// 功能：打印错误信息，帮助调试
make_EHelper(inv) {
  /* invalid opcode */
  // 非法操作码：指令未实现或指令字节错误

  uint32_t temp[2];                     // 临时缓冲区（存放 8 字节指令字节）
  vaddr_t ori_eip = cpu.eip;            // 保存原始的 eip（出错位置）
  *eip = ori_eip;                       // 重置 eip 到出错位置（准备读取指令字节）
  temp[0] = instr_fetch(eip, 4);        // 读取 4 字节
  temp[1] = instr_fetch(eip, 4);        // 再读取 4 字节（共 8 字节）

  uint8_t *p = (void *)temp;            // 把 temp 当作字节数组
  printf("invalid opcode(eip = 0x%08x): %02x %02x %02x %02x %02x %02x %02x %02x ...\n\n",
      ori_eip, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  // 打印出错位置和指令字节（十六进制）
  // 例如："invalid opcode(eip = 0x8048100): 0f 38 00 11 22 33 44 55 ..."

  extern char logo [];                  // NEMU 的 logo（包含 PA 信息）
  printf("There are two cases which will trigger this unexpected exception:\n"
      "1. The instruction at eip = 0x%08x is not implemented.\n"
      "2. Something is implemented incorrectly.\n", ori_eip);
  // 提示：有两种可能导致这个错误
  //   情况 1：这条指令还没有实现（需要去 exec.c 的 opcode_table 添加）
  //   情况 2：之前的代码有 bug（导致跳转到错误的地址，或者栈被破坏）

  printf("Find this eip(0x%08x) in the disassembling result to distinguish which case it is.\n\n", ori_eip);
  // 建议：用 objdump 反汇编客户程序，找到这个 eip 对应的指令
  //       如果是正常的指令 → 情况 1（未实现）
  //       如果是奇怪的字节 → 情况 2（代码错误）

  printf("\33[1;31mIf it is the first case, see\n%s\nfor more details.\n\nIf it is the second case, remember:\n"
      "* The machine is always right!\n"
      "* Every line of untested code is always wrong!\33[0m\n\n", logo);
  // \33[1;31m = 红色粗体，\33[0m = 恢复默认颜色
  // 提醒：
  //   - 情况 1：查看 PA 讲义，添加指令实现
  //   - 情况 2：机器永远是对的！每一行未测试的代码都是错的！
  //           （调试哲学：不要怀疑硬件，要怀疑自己的代码）

  nemu_state = NEMU_END;                // 设置 NEMU 状态为结束
                                        // cpu_exec 会检测到这个状态并退出

  print_asm("invalid opcode");          // 打印汇编指令："invalid opcode"
}

// nemu_trap 指令：NEMU 特殊陷阱
// 这是 NEMU 自己定义的伪指令（操作码 0xd6，真实 x86 中未使用）
// 用途：让客户程序主动结束（类似 exit 系统调用）
// 语义：根据 eax 的值判断程序是正常结束还是异常结束
//       eax = 0 → 程序正常结束（GOOD TRAP）
//       eax ≠ 0 → 程序异常结束（BAD TRAP）
make_EHelper(nemu_trap) {
  print_asm("nemu trap (eax = %d)", cpu.eax);
  // 打印汇编指令和退出码，例如："nemu trap (eax = 0)"

  printf("\33[1;31mnemu: HIT %s TRAP\33[0m at eip = 0x%08x\n\n",
      (cpu.eax == 0 ? "GOOD" : "BAD"), cpu.eip);
  // 红色打印：
  //   eax = 0 → "nemu: HIT GOOD TRAP at eip = 0x..."（测试通过）
  //   eax ≠ 0 → "nemu: HIT BAD TRAP at eip = 0x..."（测试失败）

  nemu_state = NEMU_END;                // 设置 NEMU 状态为结束
                                        // 这样 cpu_exec 会停止执行，返回调试器

#ifdef DIFF_TEST
  extern void diff_test_skip_qemu();
  diff_test_skip_qemu();                // 差分测试时跳过 QEMU 的对比
                                        // 因为 nemu_trap 是 NEMU 专有的，QEMU 没有
#endif
}

// ──────────────────────────────────────────────────────────────────────────────
// nemu_trap 的使用方法：
//
// 在测试程序的 C 代码中定义一个内联汇编宏：
//
//   #define nemu_trap(code) asm volatile(".byte 0xd6" : :"a"(code))
//
// 然后在程序末尾调用：
//
//   void _start() {
//     int result = test_function();
//     nemu_trap(result == expected ? 0 : 1);  // 0=成功，1=失败
//   }
//
// 或者更简单：
//
//   void _start() {
//     test_all();
//     nemu_trap(0);  // 如果能执行到这里，说明所有测试通过
//   }
//
// 这样运行 NEMU 时会看到：
//   nemu: HIT GOOD TRAP at eip = 0x8048100  ← 测试通过
// 或者：
//   nemu: HIT BAD TRAP at eip = 0x8048100   ← 测试失败
//
// ──────────────────────────────────────────────────────────────────────────────
// invalid opcode 调试技巧：
//
// 1. 看到 "invalid opcode(eip = 0x8048100)" 时：
//    $ objdump -d guest-program > dis.txt
//    $ grep 8048100 dis.txt
//    看看这个地址是什么指令
//
// 2. 如果是正常指令（例如 "imul $0x5, %eax"）：
//    → 情况 1：去 exec.c 的 opcode_table 添加这条指令的实现
//
// 3. 如果是奇怪的字节（例如 "00 00 00 00"）：
//    → 情况 2：你的代码有 bug
//    常见原因：
//      - 函数返回地址被破坏（栈溢出）
//      - 跳转到错误的地址（指针计算错误）
//      - 数据被当成指令执行（代码段和数据段混淆）
//
// 4. 使用 GDB 调试：
//    (nemu) b *0x8048100
//    (nemu) c
//    (nemu) si
//    单步执行，看看是怎么跳到这个错误地址的
//
// ──────────────────────────────────────────────────────────────────────────────
// "The machine is always right!" 的含义：
//
// 这是计算机科学的一条重要原则：
//   - 计算机是确定性的：相同的输入 → 相同的输出
//   - 如果程序行为不符合预期，不是计算机错了，而是程序员的理解错了
//   - 不要说"奇怪，这里应该是对的"，而是要找出为什么"看起来对"的代码实际上是错的
//
// 推论：
//   - 不要依赖"运气"（"这次能跑就行"）
//   - 不要忽略警告（"编译器太严格了"）
//   - 不要猜测（"我觉得应该是这样"）→ 要测试、验证、理解
//
// Every line of untested code is always wrong!
//   - 没有测试过的代码，默认都是错的
//   - 只有测试通过的代码，才能认为是对的
//   - 即使测试通过，也可能有边界情况没覆盖到
// ──────────────────────────────────────────────────────────────────────────────
