// reg.c - 寄存器实现和测试（PA1 核心文件）
// 功能：定义 CPU 寄存器结构体实例，提供寄存器名称数组，测试寄存器实现

#include "nemu.h"         // NEMU 全局定义（包含 CPU_state 结构体定义）
#include <stdlib.h>       // rand(), srand()
#include <time.h>         // time()

CPU_state cpu;            // 全局变量：CPU 状态（所有寄存器）
                          // 这是整个 NEMU 中唯一的 CPU 实例
                          // 所有指令执行函数都通过修改这个全局变量来模拟 CPU 行为

// 寄存器名称数组：用于打印调试信息和 info r 命令
const char *regsl[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
// regsl = registers long（32 位寄存器名称）
// 索引对应寄存器编号：0=eax, 1=ecx, 2=edx, 3=ebx, 4=esp, 5=ebp, 6=esi, 7=edi

const char *regsw[] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
// regsw = registers word（16 位寄存器名称）
// ax = eax 的低 16 位，cx = ecx 的低 16 位，依此类推

const char *regsb[] = {"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"};
// regsb = registers byte（8 位寄存器名称）
// 前 4 个（al/cl/dl/bl）= 低 8 位
// 后 4 个（ah/ch/dh/bh）= 第二个字节（bit 8-15）

// reg_test: 测试寄存器结构体实现是否正确
// 功能：在 NEMU 启动时执行，验证 union 和位域的实现
// 原理：给寄存器赋随机值，然后检查各个别名是否正确对应
// 这个测试确保了 reg.h 中定义的 union 结构体能正确工作
void reg_test() {
  srand(time(0));                       // 用当前时间初始化随机数生成器
                                        // 这样每次运行 NEMU 都会测试不同的值
  uint32_t sample[8];                   // 8 个随机数（对应 8 个通用寄存器）
  uint32_t eip_sample = rand();         // eip 的随机值
  cpu.eip = eip_sample;                 // 给 eip 赋值

  int i;
  for (i = R_EAX; i <= R_EDI; i ++) {   // 遍历 8 个通用寄存器（eax-edi）
    //Log("test %s", reg_name(i, 4));
    sample[i] = rand();                 // 生成随机值
    reg_l(i) = sample[i];               // 给 32 位寄存器赋值
                                        // reg_l(i) 展开成 cpu.gpr[i]._32
    assert(reg_w(i) == (sample[i] & 0xffff));
    // 测试 1：16 位别名是否正确
    // reg_w(i) 应该等于 sample[i] 的低 16 位
    // 例如：如果 eax = 0x12345678，那么 ax 应该 = 0x5678
  }

  // 测试 2：8 位别名是否正确
  // 检查所有 8 位寄存器（al/ah/bl/bh/cl/ch/dl/dh）
  assert(reg_b(R_AL) == (sample[R_EAX] & 0xff));
  // al = eax 的低 8 位（bit 0-7）
  // 例如：如果 eax = 0x12345678，那么 al = 0x78

  assert(reg_b(R_AH) == ((sample[R_EAX] >> 8) & 0xff));
  // ah = eax 的第二个字节（bit 8-15）
  // 例如：如果 eax = 0x12345678，那么 ah = 0x56

  assert(reg_b(R_BL) == (sample[R_EBX] & 0xff));
  assert(reg_b(R_BH) == ((sample[R_EBX] >> 8) & 0xff));
  assert(reg_b(R_CL) == (sample[R_ECX] & 0xff));
  assert(reg_b(R_CH) == ((sample[R_ECX] >> 8) & 0xff));
  assert(reg_b(R_DL) == (sample[R_EDX] & 0xff));
  assert(reg_b(R_DH) == ((sample[R_EDX] >> 8) & 0xff));
  // 同样检查 ebx/ecx/edx 的低字节和第二字节

  // 测试 3：直接访问是否正确
  // 检查通过 cpu.eax/cpu.ecx 等直接访问是否和数组访问一致
  assert(sample[R_EAX] == cpu.eax);
  assert(sample[R_ECX] == cpu.ecx);
  assert(sample[R_EDX] == cpu.edx);
  assert(sample[R_EBX] == cpu.ebx);
  assert(sample[R_ESP] == cpu.esp);
  assert(sample[R_EBP] == cpu.ebp);
  assert(sample[R_ESI] == cpu.esi);
  assert(sample[R_EDI] == cpu.edi);
  // 验证 union 的两种访问方式（数组 vs 直接访问）是等价的

  assert(eip_sample == cpu.eip);        // 测试 4：eip 赋值是否正确
}

// ──────────────────────────────────────────────────────────────────────────────
// reg_test 测试的内容和原理：
//
// 这个测试验证了 reg.h 中定义的 union 结构体是否正确实现了寄存器别名机制：
//
// typedef union {
//   union {
//     uint32_t _32;   // 32 位别名（例如 eax）
//     uint16_t _16;   // 16 位别名（例如 ax，低 16 位）
//     uint8_t _8[2];  // 8 位别名（_8[0]=al 低字节，_8[1]=ah 第二字节）
//   } gpr[8];         // 8 个通用寄存器数组
//   struct {          // 直接访问的别名
//     uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
//   };
// } CPU_state;
//
// 测试流程：
//   1. 给 eax 赋值 0x12345678
//   2. 检查 ax 是否 = 0x5678（低 16 位）
//   3. 检查 al 是否 = 0x78（低 8 位）
//   4. 检查 ah 是否 = 0x56（第二字节）
//   5. 检查 cpu.eax 是否 = 0x12345678（直接访问）
//
// 如果所有断言都通过，说明寄存器结构体实现正确
// 如果有断言失败，说明 union 的对齐或定义有问题，需要修改 reg.h
//
// ──────────────────────────────────────────────────────────────────────────────
// 为什么需要 reg_test？
//
// C 语言的 union 和位域是底层特性，容易出错：
//   - 字节序问题（大端 vs 小端）
//   - 对齐问题（结构体成员的内存布局）
//   - 编译器差异（不同编译器可能有不同的实现）
//
// reg_test 在 NEMU 启动时自动执行，确保寄存器实现在当前平台上正确工作
// 如果测试失败，NEMU 会立即崩溃（assert），防止后续运行产生错误结果
//
// ──────────────────────────────────────────────────────────────────────────────
// x86 寄存器别名机制：
//
// x86 的寄存器有多个名字（别名），指向同一块存储：
//
//   ┌──────────────────────────────────┐
//   │           EAX (32位)             │  ← reg_l(R_EAX) = cpu.gpr[0]._32
//   ├─────────────────┬────────────────┤
//   │     高16位      │  AX (16位)     │  ← reg_w(R_EAX) = cpu.gpr[0]._16
//   │                 ├────────┬───────┤
//   │                 │ AH (8) │ AL(8) │  ← reg_b(R_AH) / reg_b(R_AL)
//   └─────────────────┴────────┴───────┘
//     31           16 15     8 7     0
//
// 修改任何一个别名，其他别名也会改变：
//   mov $0x12345678, %eax  → eax = 0x12345678
//   mov $0x00, %al         → eax = 0x12345600（只改变 al）
//   mov $0xff, %ah         → eax = 0x1234ff00（只改变 ah）
//
// 这是 x86 架构为了向后兼容 8086（16位）而设计的机制
// NEMU 用 C 的 union 特性来模拟这种硬件行为
//
// ──────────────────────────────────────────────────────────────────────────────
// reg_l / reg_w / reg_b 宏的定义（在 reg.h 中）：
//
// #define reg_l(index) (cpu.gpr[index]._32)    // 32 位访问
// #define reg_w(index) (cpu.gpr[index]._16)    // 16 位访问
// #define reg_b(index) (cpu.gpr[check_reg_index(index)]._8[index & 0x1])
// // 8 位访问比较复杂：
// //   index 0-3 → _8[0]（低字节：al/cl/dl/bl）
// //   index 4-7 → _8[1]（第二字节：ah/ch/dh/bh）
//
// 用法示例：
//   reg_l(R_EAX) = 0x12345678;  // eax = 0x12345678
//   uint16_t ax = reg_w(R_EAX); // ax = 0x5678
//   uint8_t al = reg_b(R_AL);   // al = 0x78
//
// ──────────────────────────────────────────────────────────────────────────────
