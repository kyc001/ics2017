// 头文件保护：防止重复包含导致重定义错误
// 第一次 #include 本文件时 __REG_H__ 未定义，条件成立，继续往下
// 立即定义 __REG_H__，之后再次包含时会被 #ifndef 挡住
#ifndef __REG_H__
#define __REG_H__

#include "common.h"     // 引入公共类型定义(uint32_t / rtlreg_t / vaddr_t / bool 等)

// 下面 3 个 enum 给寄存器"编号"，用于指令译码时按编号快速索引
// enum 不写具体值时默认从 0 开始逐个 +1，即 R_EAX=0, R_ECX=1, R_EDX=2...
// 注意：这个顺序必须严格遵守 i386 指令编码规范，不能随意改动！
// 因为指令译码时会直接拿编码中的数字当数组下标来访问对应寄存器
enum { R_EAX, R_ECX, R_EDX, R_EBX, R_ESP, R_EBP, R_ESI, R_EDI };  // 32 位寄存器编号 0~7
enum { R_AX, R_CX, R_DX, R_BX, R_SP, R_BP, R_SI, R_DI };          // 16 位寄存器编号 0~7
enum { R_AL, R_CL, R_DL, R_BL, R_AH, R_CH, R_DH, R_BH };          // 8  位寄存器编号 0~7

/* TODO: Re-organize the `CPU_state' structure to match the register
 * encoding scheme in i386 instruction format. For example, if we
 * access cpu.gpr[3]._16, we will get the `bx' register; if we access
 * cpu.gpr[1]._8[1], we will get the 'ch' register. Hint: Use `union'.
 * For more details about the register encoding scheme, see i386 manual.
 */

// GPR = General Purpose Register（通用寄存器）
// 用 union（联合体）实现"同一块内存的多种访问方式"
// union 的所有成员共用同一块存储空间，修改任意一个成员都会影响其他成员
// 这正好对应了 x86 寄存器的特点：eax/ax/al/ah 本质是同一个寄存器的不同部分
typedef union {
  uint32_t _32;     // 把这 4 字节当成一个 32 位整数看 → 对应 eax（完整的 32 位）
  uint16_t _16;     // 只看低 2 个字节（16 位）        → 对应 ax（eax 的低 16 位）
  uint8_t  _8[2];   // 把它拆成 2 个字节：
                    //   _8[0] = 最低字节（bit 0-7）   → 对应 al（eax 的最低 8 位）
                    //   _8[1] = 次低字节（bit 8-15）  → 对应 ah（eax 的次低 8 位）
}GPR;               // 所以当你修改 al 时，ax 和 eax 的值也会跟着变——它们本就是同一块内存

// CPU_state 结构体：模拟出来的"CPU 当前全部状态"
// 这个结构体是整个 NEMU 的核心数据结构之一，代表了我们虚拟 CPU 的所有寄存器
typedef struct {
  // ↓↓↓ 这个匿名 union 是 reg.h 的核心（PA1 的 TODO 重点就是理解它）↓↓↓
  // 匿名 union 让它的所有成员共享同一块内存，可以用不同方式访问同一组寄存器
  union {
    GPR gpr[8];                 // 视角一：8 个通用寄存器组成的数组，按"编号"访问
                                //   例如 cpu.gpr[0] 就是第 0 个寄存器（eax）
                                //   例如 cpu.gpr[3]._16 就是第 3 个寄存器的低 16 位（bx）
    struct {                    // 视角二：同一块内存又起了 8 个名字，按"名字"访问
      rtlreg_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
      //         ↑这 8 个变量和上面的 gpr[8] 数组占用完全相同的内存！
      //         所以 cpu.eax 其实就等于 cpu.gpr[0]._32（指向同一个地址）
      //         修改 cpu.eax 会立即反映到 cpu.gpr[0]._32 上，反之亦然
    };
  };
  /* Do NOT change the order of the GPRs' definitions. */
  // ↑↑↑ 千万别改顺序！这个顺序是 i386 指令编码标准规定的：
  // eax=0, ecx=1, edx=2, ebx=3, esp=4, ebp=5, esi=6, edi=7
  // 指令译码时会直接拿编号当数组下标，顺序错了整个译码就乱套了

  /* In NEMU, rtlreg_t is exactly uint32_t. This makes RTL instructions
   * in PA2 able to directly access these registers.
   */
  // rtlreg_t 其实就是 uint32_t（32 位无符号整数）
  // PA2 实现 RTL（Register Transfer Language，寄存器传输语言）指令时会直接用它

  vaddr_t eip;      // eip = 程序计数器（Program Counter）= 下一条要执行指令的地址
                    // 这是 CPU 最核心的寄存器之一："取指令→执行→更新eip"是 CPU 工作的永恒循环
                    // 在 x86 32位模式下叫 eip，64位模式叫 rip

  // eflags（标志寄存器）同样用 union：既能按"单个标志位"取，也能按"整个 32 位"取
  union {
    struct {                    // 视角一：用位域（bit-field）单独访问每个标志位
                                // 冒号后面的数字表示这个字段占几个 bit
      uint32_t CF : 1;          // bit 0：进位标志 Carry Flag
                                //   加法最高位产生进位时置1，减法需要借位时置1
      uint32_t    : 5;          // bit 1-5：保留位（没名字 = 占位用，暂时不关心）
      uint32_t ZF : 1;          // bit 6：零标志 Zero Flag
                                //   运算结果为 0 时置1（例如 5-5=0 会让 ZF=1）
      uint32_t SF : 1;          // bit 7：符号标志 Sign Flag
                                //   运算结果为负数（最高位=1）时置1
      uint32_t    : 1;          // bit 8：保留
      uint32_t IF : 1;          // bit 9：中断允许标志 Interrupt Flag
                                //   IF=1 时允许响应外部硬件中断（PA3/PA4 会用到）
      uint32_t    : 1;          // bit 10：保留
      uint32_t OF : 1;          // bit 11：溢出标志 Overflow Flag
                                //   有符号数运算溢出时置1（例如 127+1=-128 会让 OF=1）
      uint32_t    : 20;         // bit 12-31：其余保留位
    };
    uint32_t val;               // 视角二：把整个 eflags 当作一个 32 位数来读写
                                //   有时需要一次性保存/恢复整个 eflags，用 val 就很方便
  } eflags;

  uint16_t cs;                  // 代码段寄存器 Code Segment
                                //   PA3 处理异常/中断时会把它压栈保存
  struct {                      // IDT 寄存器：指向中断描述符表（Interrupt Descriptor Table）
    uint16_t limit;             //   limit = IDT 的长度（字节数 - 1）
    uint32_t base;              //   base  = IDT 的起始地址
  } idtr;                       //   CPU 通过 lidt 指令加载这个寄存器（PA3 异常处理会用）

  uint32_t cr0;     // 控制寄存器 0：其中有一位用来开启分页机制（PA4 虚拟内存会用）
  uint32_t cr3;     // 控制寄存器 3：存放页目录的物理基址（PA4 分页时作为页表的根）
  bool INTR;        // 是否有待处理的硬件中断（PA4 实现时钟中断时会用到这个标志）

} CPU_state;        // 这一整个结构体 = 我们用 C 语言模拟出来的"CPU 当前全部状态"
                    // NEMU 的本质就是不断更新这个结构体，让它"表现得像真的 CPU 在运行"

extern CPU_state cpu;   // 声明全局变量 cpu（真正的定义在某个 .c 文件里）
                        // 整个 NEMU 都通过这个全局变量 cpu 来访问和修改寄存器
                        // 例如：cpu.eax = 100; 或 cpu.eip += 4; 等

// 内联函数：检查寄存器编号是否在合法范围 [0, 7] 内
// static inline = 在编译时直接展开，不产生函数调用开销
static inline int check_reg_index(int index) {
  assert(index >= 0 && index < 8);  // 如果编号越界就立即报错停止
  return index;                     // 合法就原样返回
}

// 下面 3 个宏 = "按编号取某个寄存器"的快捷写法
// 宏在编译预处理阶段会被文本替换，所以 reg_l(2) 会被替换成 cpu.gpr[check_reg_index(2)]._32
#define reg_l(index) (cpu.gpr[check_reg_index(index)]._32)              // 取第 index 个寄存器的 32 位
                                                                         // 例如 reg_l(0) = cpu.gpr[0]._32 = eax
#define reg_w(index) (cpu.gpr[check_reg_index(index)]._16)              // 取第 index 个寄存器的低 16 位
                                                                         // 例如 reg_w(0) = cpu.gpr[0]._16 = ax
#define reg_b(index) (cpu.gpr[check_reg_index(index) & 0x3]._8[index >> 2])
// ↑ 取 8 位寄存器比较复杂，因为 al/cl/dl/bl（编号0-3）和 ah/ch/dh/bh（编号4-7）的映射关系特殊
// 举例说明 reg_b 的工作原理：
//   reg_b(0) = al:  (0 & 0x3) = 0 → cpu.gpr[0],  (0 >> 2) = 0 → _8[0] → al（eax 最低字节）
//   reg_b(4) = ah:  (4 & 0x3) = 0 → cpu.gpr[0],  (4 >> 2) = 1 → _8[1] → ah（eax 次低字节）
//   reg_b(1) = cl:  (1 & 0x3) = 1 → cpu.gpr[1],  (1 >> 2) = 0 → _8[0] → cl（ecx 最低字节）
//   reg_b(5) = ch:  (5 & 0x3) = 1 → cpu.gpr[1],  (5 >> 2) = 1 → _8[1] → ch（ecx 次低字节）
// 规律：(index & 0x3) 取低 2 位选哪个寄存器（0-3 对应 eax-ebx）
//       (index >> 2) 右移 2 位选字节 0 还是字节 1（即 l 还是 h）

extern const char* regsl[];     // 声明：32 位寄存器名字符串数组（定义在 reg.c）
extern const char* regsw[];     // 声明：16 位寄存器名字符串数组
extern const char* regsb[];     // 声明：8  位寄存器名字符串数组
// 这些数组在 info r 等调试命令中用来打印寄存器名字，例如 regsl[0] = "eax"

// 根据编号和宽度返回对应的寄存器名字符串
// 例如 reg_name(0, 4) 返回 "eax"，reg_name(0, 1) 返回 "al"
static inline const char* reg_name(int index, int width) {
  assert(index >= 0 && index < 8);  // 编号必须在 0-7 范围内
  switch (width) {
    case 4: return regsl[index];    // 宽度 4 字节 → 32 位寄存器名
    case 1: return regsb[index];    // 宽度 1 字节 → 8  位寄存器名
    case 2: return regsw[index];    // 宽度 2 字节 → 16 位寄存器名
    default: assert(0);             // 其他宽度非法，直接报错
  }
}

#endif      // __REG_H__ 的结束标记，与开头的 #ifndef 配对
