// logic.c - 逻辑运算指令的执行函数（PA2 核心文件之一）
// 功能：实现 x86 的逻辑运算指令（test、and、or、xor、not 等）和移位指令（shl、shr、rol、ror、sar 等）
// 每个 make_EHelper 函数对应一条 x86 指令的执行逻辑

#include "cpu/exec.h"     // 指令执行框架的头文件（包含 rtl 函数声明、操作数结构等）

// test 指令：测试（按位与但不保存结果，只更新标志位）
// 语义：temp = dest & src，只更新 EFLAGS，不修改 dest
// 用途：常用于测试某些位是否为 1（例如 test eax, 1 测试 eax 的最低位）
make_EHelper(test) {
  rtl_and(&t2, &id_dest->val, &id_src->val);   // t2 = dest & src（按位与）
                                                // rtl_xxx 是 RTL（寄存器传输语言）层的操作
                                                // id_dest、id_src 是译码阶段解析出的操作数
  rtl_update_ZFSF(&t2, id_dest->width);         // 根据结果 t2 更新 ZF（零标志）和 SF（符号标志）
                                                // ZF：结果为 0 则置 1
                                                // SF：结果最高位为 1（负数）则置 1
  rtl_li(&t0, 0);                               // rtl_li = load immediate，把立即数 0 加载到 t0
  rtl_set_CF(&t0);                              // CF（进位标志）= 0
  rtl_set_OF(&t0);                              // OF（溢出标志）= 0
                                                // test 指令规定：CF 和 OF 总是被清零

  print_asm_template2(test);                    // 打印汇编指令（调试输出）
                                                // 例如："test eax, ebx" 或 "test $0x1, %eax"
}

// and 指令：按位与
// 语义：dest = dest & src，同时更新 EFLAGS
// 用途：清除某些位（例如 and eax, 0xFFFFFFFE 清除最低位）
make_EHelper(and) {
  rtl_and(&t2, &id_dest->val, &id_src->val);   // t2 = dest & src
  operand_write(id_dest, &t2);                  // 把结果 t2 写回目的操作数
                                                // operand_write 会根据操作数类型（寄存器/内存）选择写入方式

  rtl_update_ZFSF(&t2, id_dest->width);         // 更新 ZF 和 SF
  rtl_li(&t0, 0);
  rtl_set_CF(&t0);                              // CF = 0（按位运算总是清除进位标志）
  rtl_set_OF(&t0);                              // OF = 0（按位运算总是清除溢出标志）

  // Log("pa2-debug: and result=0x%08x", t2);  // 调试输出（已注释）

  print_asm_template2(and);                     // 打印汇编指令，例如："and %eax, %ebx"
}

// xor 指令：按位异或
// 语义：dest = dest ^ src，同时更新 EFLAGS
// 用途：翻转某些位，或用 xor eax, eax 快速清零（比 mov eax, 0 短 1 字节）
make_EHelper(xor) {
  rtl_xor(&t2, &id_dest->val, &id_src->val);   // t2 = dest ^ src（按位异或）
  operand_write(id_dest, &t2);                  // 写回结果

  rtl_update_ZFSF(&t2, id_dest->width);         // 更新 ZF 和 SF
  rtl_li(&t0, 0);
  rtl_set_CF(&t0);                              // CF = 0
  rtl_set_OF(&t0);                              // OF = 0

  // Log("pa2-debug: xor result=0x%08x", t2);  // 调试输出（已注释）

  print_asm_template2(xor);                     // 打印汇编指令
}

// or 指令：按位或
// 语义：dest = dest | src，同时更新 EFLAGS
// 用途：设置某些位为 1（例如 or eax, 0x1 设置最低位为 1）
make_EHelper(or) {
  rtl_or(&t2, &id_dest->val, &id_src->val);    // t2 = dest | src（按位或）
  operand_write(id_dest, &t2);                  // 写回结果

  rtl_update_ZFSF(&t2, id_dest->width);         // 更新 ZF 和 SF
  rtl_li(&t0, 0);
  rtl_set_CF(&t0);                              // CF = 0
  rtl_set_OF(&t0);                              // OF = 0

  print_asm_template2(or);                      // 打印汇编指令
}

// rol 指令：循环左移（Rotate Left）
// 语义：把 dest 的位循环左移 src 次，移出最高位的位循环到最低位
// 例如：8 位的 0b11001010 循环左移 2 次 → 0b00101011
make_EHelper(rol) {
  uint32_t bits = id_dest->width * 8;           // 操作数的位数（1 字节 = 8 位，2 字节 = 16 位，4 字节 = 32 位）
  uint32_t mask = bits == 32 ? 0xffffffffu : ((1u << bits) - 1);
  // 位掩码：用于截断高位
  // 例如 8 位：mask = (1 << 8) - 1 = 0xFF
  //      16 位：mask = (1 << 16) - 1 = 0xFFFF
  //      32 位：mask = 0xFFFFFFFF（特判，因为 1u << 32 是未定义行为）
  uint32_t value = id_dest->val & mask;         // 取出 dest 的有效位
  uint32_t count = (id_src->val & 0x1f) % bits; // 移位次数 = src 的低 5 位（最多 31）再对位数取模
                                                // x86 规定：移位次数只看 src 的低 5 位，且会对位数取模
  uint32_t result = value;                      // 默认结果 = 原值（如果 count == 0 就不移位）

  if (count != 0) {                             // 只有 count 非 0 才执行移位
    result = ((value << count) | (value >> (bits - count))) & mask;
    // 循环左移：高位部分 = value 左移 count 位，低位部分 = value 右移 (bits - count) 位
    // 例如 8 位 0b11001010 循环左移 2 位：
    //   高位部分 = 0b11001010 << 2 = 0b00101000
    //   低位部分 = 0b11001010 >> 6 = 0b00000011
    //   结果 = 0b00101000 | 0b00000011 = 0b00101011
    rtl_li(&t0, result & 0x1);                  // CF = 结果的最低位（刚才从最高位循环过来的那一位）
    rtl_set_CF(&t0);
    if (count == 1) {                           // 如果只移 1 位，需要设置 OF
      uint32_t of = ((result >> (bits - 1)) ^ (result & 0x1)) & 0x1;
      // OF = 结果的最高位 XOR 最低位（判断符号是否改变）
      rtl_li(&t0, of);
      rtl_set_OF(&t0);
    }
    // 如果 count > 1，OF 的值是未定义的，不设置
  }

  rtl_li(&t2, result);                          // 把结果加载到 t2
  operand_write(id_dest, &t2);                  // 写回目的操作数
  print_asm_template2(rol);                     // 打印汇编指令，例如："rol $2, %eax"
}

// ror 指令：循环右移（Rotate Right）
// 语义：把 dest 的位循环右移 src 次，移出最低位的位循环到最高位
make_EHelper(ror) {
  uint32_t bits = id_dest->width * 8;
  uint32_t mask = bits == 32 ? 0xffffffffu : ((1u << bits) - 1);
  uint32_t value = id_dest->val & mask;
  uint32_t count = (id_src->val & 0x1f) % bits;
  uint32_t result = value;

  if (count != 0) {
    result = ((value >> count) | (value << (bits - count))) & mask;
    // 循环右移：低位部分 = value 右移 count 位，高位部分 = value 左移 (bits - count) 位
    rtl_li(&t0, (result >> (bits - 1)) & 0x1);  // CF = 结果的最高位（刚才从最低位循环过来的那一位）
    rtl_set_CF(&t0);
    if (count == 1) {
      uint32_t msb = (result >> (bits - 1)) & 0x1;    // 最高位
      uint32_t next = (result >> (bits - 2)) & 0x1;   // 次高位
      rtl_li(&t0, msb ^ next);                  // OF = 最高位 XOR 次高位
      rtl_set_OF(&t0);
    }
  }

  rtl_li(&t2, result);
  operand_write(id_dest, &t2);
  print_asm_template2(ror);
}

// sar 指令：算术右移（Shift Arithmetic Right）
// 语义：把 dest 右移 src 次，空出的高位用符号位填充（保持符号不变）
// 区别于逻辑右移（shr）：sar 保持符号，shr 用 0 填充
// 用途：有符号数除以 2 的幂（例如 sar eax, 1 相当于 eax /= 2）
make_EHelper(sar) {
  uint32_t count = id_src->val & 0x1f;          // 移位次数（低 5 位）
  uint32_t result = id_dest->val;               // 默认结果 = 原值
  if (count != 0) {
    uint32_t cf = (id_dest->val >> (count - 1)) & 0x1;
    // CF = 最后被移出的那一位
    switch (id_dest->width) {                   // 根据操作数宽度选择有符号右移
      case 1: result = (int8_t)id_dest->val >> count; break;   // 8 位有符号右移
      case 2: result = (int16_t)id_dest->val >> count; break;  // 16 位有符号右移
      case 4: result = (int32_t)id_dest->val >> count; break;  // 32 位有符号右移
      default: assert(0);                       // 其他宽度非法
    }
    // 强制转换成有符号类型后右移，C 语言会自动用符号位填充高位
    rtl_li(&t0, cf);
    rtl_set_CF(&t0);                            // 设置 CF
    rtl_li(&t0, 0);
    rtl_set_OF(&t0);                            // OF = 0（sar 总是清除 OF）
  }
  rtl_li(&t2, result);
  operand_write(id_dest, &t2);
  if (count != 0) {
    rtl_update_ZFSF(&t2, id_dest->width);       // 更新 ZF 和 SF
  }

  print_asm_template2(sar);
}

// shl 指令：逻辑左移（Shift Left）
// 语义：把 dest 左移 src 次，低位用 0 填充
// 用途：无符号数乘以 2 的幂（例如 shl eax, 2 相当于 eax *= 4）
make_EHelper(shl) {
  uint32_t count = id_src->val & 0x1f;          // 移位次数
  uint32_t bits = id_dest->width * 8;           // 位数
  uint32_t mask = bits == 32 ? 0xffffffffu : ((1u << bits) - 1);
  uint32_t value = id_dest->val & mask;         // 取有效位
  uint32_t result = value;
  if (count != 0) {
    uint32_t cf = (value >> (bits - count)) & 0x1;
    // CF = 最后被移出最高位的那一位
    result = (value << count) & mask;           // 左移并截断高位
    rtl_li(&t0, cf);
    rtl_set_CF(&t0);
    if (count == 1) {                           // 只移 1 位时设置 OF
      uint32_t of = ((result >> (bits - 1)) & 0x1) ^ cf;
      // OF = 结果的最高位 XOR 移出的那一位（判断符号是否改变）
      rtl_li(&t0, of);
      rtl_set_OF(&t0);
    }
    else {                                      // 移位次数 > 1，OF 未定义，这里设为 0
      rtl_li(&t0, 0);
      rtl_set_OF(&t0);
    }
  }
  rtl_li(&t2, result);
  operand_write(id_dest, &t2);
  if (count != 0) {
    rtl_update_ZFSF(&t2, id_dest->width);
  }

  print_asm_template2(shl);
}

// shr 指令：逻辑右移（Shift Right）
// 语义：把 dest 右移 src 次，高位用 0 填充
// 用途：无符号数除以 2 的幂（例如 shr eax, 3 相当于 eax /= 8）
make_EHelper(shr) {
  uint32_t count = id_src->val & 0x1f;
  uint32_t bits = id_dest->width * 8;
  uint32_t mask = bits == 32 ? 0xffffffffu : ((1u << bits) - 1);
  uint32_t value = id_dest->val & mask;
  uint32_t result = value;
  if (count != 0) {
    uint32_t cf = (value >> (count - 1)) & 0x1; // CF = 最后被移出的那一位
    result = value >> count;                    // 逻辑右移（高位自动填 0）
    rtl_li(&t0, cf);
    rtl_set_CF(&t0);
    if (count == 1) {
      uint32_t of = (value >> (bits - 1)) & 0x1;
      // OF = 原值的最高位（判断符号是否改变）
      rtl_li(&t0, of);
      rtl_set_OF(&t0);
    }
    else {
      rtl_li(&t0, 0);
      rtl_set_OF(&t0);
    }
  }
  rtl_li(&t2, result);
  operand_write(id_dest, &t2);
  if (count != 0) {
    rtl_update_ZFSF(&t2, id_dest->width);
  }

  print_asm_template2(shr);
}

// shld 指令：双精度左移（Shift Left Double）
// 语义：把 dest 左移 count 次，空出的低位用 src2 的高位填充
// 这是一个三操作数指令：shld count, src2, dest
// 用途：实现跨寄存器的移位操作（例如处理 64 位数）
make_EHelper(shld) {
  uint32_t count = id_src->val & 0x1f;          // 移位次数
  uint32_t bits = id_dest->width * 8;
  uint32_t mask = bits == 32 ? 0xffffffffu : ((1u << bits) - 1);
  uint32_t dest = id_dest->val & mask;          // 目的操作数
  uint32_t src = id_src2->val & mask;           // 第二个源操作数
  uint32_t result = dest;

  if (count != 0) {
    if (count < bits) {
      uint32_t cf = (dest >> (bits - count)) & 0x1;
      result = ((dest << count) | (src >> (bits - count))) & mask;
      // dest 左移 count 位，低位用 src 的高 count 位填充
      rtl_li(&t0, cf);
      rtl_set_CF(&t0);
    }
    else {                                      // count >= bits，结果完全来自 src
      result = (src << (count - bits)) & mask;
      rtl_li(&t0, 0);
      rtl_set_CF(&t0);
    }
    if (count == 1) {
      uint32_t of = ((result >> (bits - 1)) & 0x1) ^ cpu.eflags.CF;
      rtl_li(&t0, of);
      rtl_set_OF(&t0);
    }
  }
  rtl_li(&t2, result);
  operand_write(id_dest, &t2);
  if (count != 0) {
    rtl_update_ZFSF(&t2, id_dest->width);
  }

  print_asm_template3(shld);                    // 三操作数指令
}

// shrd 指令：双精度右移（Shift Right Double）
// 语义：把 dest 右移 count 次，空出的高位用 src2 的低位填充
make_EHelper(shrd) {
  uint32_t count = id_src->val & 0x1f;
  uint32_t bits = id_dest->width * 8;
  uint32_t mask = bits == 32 ? 0xffffffffu : ((1u << bits) - 1);
  uint32_t dest = id_dest->val & mask;
  uint32_t src = id_src2->val & mask;
  uint32_t result = dest;

  if (count != 0) {
    if (count < bits) {
      uint32_t cf = (dest >> (count - 1)) & 0x1;
      result = ((dest >> count) | (src << (bits - count))) & mask;
      // dest 右移 count 位，高位用 src 的低 count 位填充
      rtl_li(&t0, cf);
      rtl_set_CF(&t0);
    }
    else {                                      // count >= bits，结果完全来自 src
      result = (src >> (count - bits)) & mask;
      rtl_li(&t0, 0);
      rtl_set_CF(&t0);
    }
    if (count == 1) {
      uint32_t of = ((dest ^ result) >> (bits - 1)) & 0x1;
      rtl_li(&t0, of);
      rtl_set_OF(&t0);
    }
  }
  rtl_li(&t2, result);
  operand_write(id_dest, &t2);
  if (count != 0) {
    rtl_update_ZFSF(&t2, id_dest->width);
  }

  print_asm_template3(shrd);
}

// setcc 指令：根据条件码设置字节（Set byte on condition）
// 语义：如果条件成立，dest = 1；否则 dest = 0
// 条件由 opcode 的低 4 位决定（例如 sete、setne、setl、setg 等）
// 用途：把 EFLAGS 的某个条件转换成一个值（0 或 1）
make_EHelper(setcc) {
  uint8_t subcode = decoding.opcode & 0xf;      // 取 opcode 的低 4 位（条件码）
                                                // 0=o, 1=no, 2=b, 3=ae, 4=e, 5=ne...
  rtl_setcc(&t2, subcode);                      // rtl_setcc 根据条件码和 EFLAGS 设置 t2 为 0 或 1
  operand_write(id_dest, &t2);                  // 写入目的操作数（通常是一个字节）

  print_asm("set%s %s", get_cc_name(subcode), id_dest->str);
  // 打印汇编指令，例如："sete %al"（当 ZF=1 时设置 al=1）
}

// not 指令：按位取反
// 语义：dest = ~dest（把所有位翻转：0→1，1→0）
// 不影响任何标志位（这是 not 指令的特点）
make_EHelper(not) {
  t0 = id_dest->val;                            // 取出目的操作数的值
  rtl_not(&t0);                                 // t0 = ~t0（按位取反）
  operand_write(id_dest, &t0);                  // 写回结果

  print_asm_template1(not);                     // 打印汇编指令，例如："not %eax"
}
