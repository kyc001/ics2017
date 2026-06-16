// cc.c - 条件码判断（Condition Code，PA2 核心文件）
// 功能：根据 EFLAGS 标志位判断条件是否成立
// 用途：支持条件跳转指令（jcc）和条件设置指令（setcc）

#include "cpu/rtl.h"      // RTL 层接口

/* Condition Code */
// 条件码：x86 架构支持 16 种条件判断，基于 EFLAGS 的不同组合

// rtl_setcc: 判断条件码是否成立
// 参数：dest = 存放结果的寄存器（1=条件成立，0=条件不成立）
//       subcode = 条件码（0-15，决定判断哪种条件）
// 功能：根据 subcode 检查 EFLAGS，判断条件是否满足
void rtl_setcc(rtlreg_t* dest, uint8_t subcode) {
  bool invert = subcode & 0x1;          // 最低位决定是否取反
                                        // 例如：0=o（溢出），1=no（未溢出）
                                        //       偶数=正常判断，奇数=取反判断

  // 条件码编号（16种）
  enum {
    CC_O, CC_NO, CC_B,  CC_NB,          // 0-3：溢出/未溢出，低于/不低于
    CC_E, CC_NE, CC_BE, CC_NBE,         // 4-7：相等/不等，低于等于/高于
    CC_S, CC_NS, CC_P,  CC_NP,          // 8-11：负数/非负，奇偶（不支持）
    CC_L, CC_NL, CC_LE, CC_NLE          // 12-15：小于/不小于，小于等于/大于
  };

  // TODO: Query EFLAGS to determine whether the condition code is satisfied.
  // dest <- ( cc is satisfied ? 1 : 0)
  // 查询 EFLAGS，判断条件码是否满足
  // dest = 条件满足 ? 1 : 0

  switch (subcode & 0xe) {              // 忽略最低位（取反标志），只看高 4 位
                                        // subcode & 0xe 把奇数变成偶数
                                        // 例如：1(no) & 0xe = 0(o)，然后最后取反

    case CC_O:                          // 0/1: o/no（Overflow / Not Overflow）
      rtl_get_OF(dest);                 // dest = OF（溢出标志）
                                        // 判断有符号运算是否溢出
      break;

    case CC_B:                          // 2/3: b/ae（Below / Above or Equal）
      rtl_get_CF(dest);                 // dest = CF（进位标志）
                                        // 用于无符号比较：CF=1 表示 a < b
                                        // b = below（低于），ae = above or equal（高于等于）
      break;

    case CC_E:                          // 4/5: e/ne（Equal / Not Equal）
      rtl_get_ZF(dest);                 // dest = ZF（零标志）
                                        // ZF=1 表示 a == b（因为 a-b == 0）
      break;

    case CC_BE:                         // 6/7: be/a（Below or Equal / Above）
      *dest = cpu.eflags.CF || cpu.eflags.ZF;
      // be = CF=1 或 ZF=1（无符号 a <= b）
      // 等价于：a < b 或 a == b
      break;

    case CC_S:                          // 8/9: s/ns（Sign / Not Sign）
      rtl_get_SF(dest);                 // dest = SF（符号标志）
                                        // SF=1 表示结果为负数
      break;

    case CC_L:                          // 12/13: l/ge（Less / Greater or Equal）
      *dest = cpu.eflags.SF != cpu.eflags.OF;
      // l = SF != OF（有符号 a < b）
      // 原理：如果 a-b 的符号位和溢出标志不一致，说明发生了溢出
      //       例如：正数 - 大负数 = 正数（应该是负数，但溢出了）→ SF=0, OF=1
      //       此时 SF != OF 为真，说明 a < b
      break;

    case CC_LE:                         // 14/15: le/g（Less or Equal / Greater）
      *dest = cpu.eflags.ZF || (cpu.eflags.SF != cpu.eflags.OF);
      // le = ZF=1 或 SF!=OF（有符号 a <= b）
      // 等价于：a < b 或 a == b
      break;

    default: panic("should not reach here");
    case CC_P: panic("n86 does not have PF");
    // CC_P（奇偶标志）：NEMU 不支持（很少用到）
  }

  if (invert) {                         // 如果 subcode 的最低位为 1
    rtl_xori(dest, dest, 0x1);          // dest = dest ^ 1（取反：0→1，1→0）
    // 例如：CC_O(0) 判断 OF，CC_NO(1) 判断 !OF
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// 条件码完整列表和使用场景：
//
// 编号 | 助记符 | 含义                 | 条件表达式              | 用途
// ─────┼────────┼──────────────────────┼─────────────────────────┼─────────────
//   0  |  o     | Overflow             | OF = 1                  | 有符号溢出
//   1  |  no    | Not Overflow         | OF = 0                  | 有符号未溢出
//   2  |  b/c   | Below / Carry        | CF = 1                  | 无符号 a < b
//   3  |  ae/nc | Above or Equal       | CF = 0                  | 无符号 a >= b
//   4  |  e/z   | Equal / Zero         | ZF = 1                  | a == b
//   5  |  ne/nz | Not Equal            | ZF = 0                  | a != b
//   6  |  be    | Below or Equal       | CF=1 或 ZF=1            | 无符号 a <= b
//   7  |  a     | Above                | CF=0 且 ZF=0            | 无符号 a > b
//   8  |  s     | Sign                 | SF = 1                  | 结果为负
//   9  |  ns    | Not Sign             | SF = 0                  | 结果非负
//  10  |  p     | Parity               | PF = 1                  | 奇偶（不支持）
//  11  |  np    | Not Parity           | PF = 0                  | 奇偶（不支持）
//  12  |  l     | Less                 | SF != OF                | 有符号 a < b
//  13  |  ge    | Greater or Equal     | SF = OF                 | 有符号 a >= b
//  14  |  le    | Less or Equal        | ZF=1 或 SF!=OF          | 有符号 a <= b
//  15  |  g     | Greater              | ZF=0 且 SF=OF           | 有符号 a > b
//
// ──────────────────────────────────────────────────────────────────────────────
// 典型用法示例：
//
// 1. 无符号比较（unsigned int a, b）：
//    cmp a, b              // 执行 a - b，更新 EFLAGS
//    jb label              // 如果 a < b（CF=1），跳转
//    ja label              // 如果 a > b（CF=0 且 ZF=0），跳转
//
// 2. 有符号比较（int a, b）：
//    cmp a, b              // 执行 a - b，更新 EFLAGS
//    jl label              // 如果 a < b（SF!=OF），跳转
//    jg label              // 如果 a > b（ZF=0 且 SF=OF），跳转
//
// 3. 相等判断：
//    cmp a, b              // 执行 a - b
//    je label              // 如果 a == b（ZF=1），跳转
//
// 4. setcc 指令（把条件转换成值）：
//    cmp eax, ebx          // 比较 eax 和 ebx
//    setl %cl              // 如果 eax < ebx（有符号），cl = 1；否则 cl = 0
//    movzx %cl, %eax       // eax = cl（零扩展）
//    // 现在 eax = (eax < ebx) ? 1 : 0
//
// ──────────────────────────────────────────────────────────────────────────────
// 有符号比较的原理（为什么 l = SF != OF）：
//
// 考虑 a - b 的结果：
//   - 如果没有溢出：SF 就是结果的符号位
//     例如：5 - 10 = -5（SF=1，OF=0）→ SF != OF = 真 → a < b ✓
//   - 如果发生溢出：SF 和真实符号相反
//     例如：-128 - 1 = 127（8位溢出，SF=0，OF=1）→ SF != OF = 真 → a < b ✓
//     例如：127 - (-1) = -128（8位溢出，SF=1，OF=1）→ SF != OF = 假 → a >= b ✓
//
// 总结：SF != OF 能正确判断有符号比较，无论是否溢出
// ──────────────────────────────────────────────────────────────────────────────
