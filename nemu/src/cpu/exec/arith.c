// arith.c - 算术运算指令的执行函数（PA2 核心文件之一）
// 功能：实现 x86 的算术运算指令（add、sub、cmp、inc、dec、neg、adc、sbb、mul、div 等）
// 这些指令会影响 EFLAGS 寄存器的标志位（CF、ZF、SF、OF 等）

#include "cpu/exec.h"     // 指令执行框架的头文件

// add 指令：加法
// 语义：dest = dest + src，同时更新 EFLAGS
// 用途：最基本的算术运算，编译器生成的加法操作都用它
make_EHelper(add) {
  rtl_add(&t2, &id_dest->val, &id_src->val);   // t2 = dest + src
  operand_write(id_dest, &t2);                  // 写回结果到目的操作数

  rtl_update_ZFSF(&t2, id_dest->width);         // 更新 ZF（零标志）和 SF（符号标志）

  // 计算 CF（进位标志）：无符号加法溢出时 CF=1
  // 判断方法：如果 sum < dest，说明发生了进位（溢出绕回了）
  rtl_sltu(&t0, &t2, &id_dest->val);            // t0 = (t2 < dest) ? 1 : 0（无符号比较）
  rtl_set_CF(&t0);                              // CF = t0

  // 计算 OF（溢出标志）：有符号加法溢出时 OF=1
  // 判断方法：两个同号数相加，结果变成异号，就是溢出
  // 等价于：~(dest ^ src) & (dest ^ sum) 的最高位
  rtl_xor(&t0, &id_dest->val, &id_src->val);   // t0 = dest ^ src（判断操作数是否同号）
  rtl_not(&t0);                                 // t0 = ~t0（取反，现在 t0=1 表示同号）
  rtl_xor(&t1, &id_dest->val, &t2);            // t1 = dest ^ sum（判断结果符号是否改变）
  rtl_and(&t0, &t0, &t1);                      // t0 = (~(dest^src)) & (dest^sum)
  rtl_msb(&t0, &t0, id_dest->width);           // t0 = t0 的最高位（符号位）
  rtl_set_OF(&t0);                              // OF = t0

  // Log("pa2-debug: add result=0x%08x", t2);  // 调试输出（已注释）

  print_asm_template2(add);                     // 打印汇编指令，例如："add %eax, %ebx"
}

// sub 指令：减法
// 语义：dest = dest - src，同时更新 EFLAGS
// 用途：减法运算，也常用于比较（cmp 指令就是 sub 的变种）
make_EHelper(sub) {
  rtl_sub(&t2, &id_dest->val, &id_src->val);   // t2 = dest - src
  operand_write(id_dest, &t2);                  // 写回结果

  rtl_update_ZFSF(&t2, id_dest->width);         // 更新 ZF 和 SF

  // 计算 CF（借位标志）：无符号减法需要借位时 CF=1
  // 判断方法：如果 dest < src，说明不够减，需要借位
  rtl_sltu(&t0, &id_dest->val, &id_src->val);  // t0 = (dest < src) ? 1 : 0
  rtl_set_CF(&t0);                              // CF = t0

  // 计算 OF（溢出标志）：有符号减法溢出时 OF=1
  // 判断方法：两个异号数相减，结果变成与被减数异号，就是溢出
  // 等价于：(dest ^ src) & (dest ^ result) 的最高位
  rtl_xor(&t0, &id_dest->val, &id_src->val);   // t0 = dest ^ src（判断操作数是否异号）
  rtl_xor(&t1, &id_dest->val, &t2);            // t1 = dest ^ result
  rtl_and(&t0, &t0, &t1);                      // t0 = (dest^src) & (dest^result)
  rtl_msb(&t0, &t0, id_dest->width);           // t0 = t0 的最高位
  rtl_set_OF(&t0);                              // OF = t0

  // Log("pa2-debug: sub result=0x%08x", t2);

  print_asm_template2(sub);
}

// cmp 指令：比较（减法但不保存结果）
// 语义：计算 dest - src，只更新 EFLAGS，不修改 dest
// 用途：比较两个数的大小，常与条件跳转指令配合使用
//       例如：cmp eax, ebx; jg label（如果 eax > ebx 就跳转）
make_EHelper(cmp) {
  rtl_sub(&t2, &id_dest->val, &id_src->val);   // t2 = dest - src

  // 更新 EFLAGS（与 sub 指令完全相同）
  rtl_update_ZFSF(&t2, id_dest->width);
  rtl_sltu(&t0, &id_dest->val, &id_src->val);
  rtl_set_CF(&t0);

  rtl_xor(&t0, &id_dest->val, &id_src->val);
  rtl_xor(&t1, &id_dest->val, &t2);
  rtl_and(&t0, &t0, &t1);
  rtl_msb(&t0, &t0, id_dest->width);
  rtl_set_OF(&t0);

  // Log("pa2-debug: cmp diff=0x%08x", t2);

  print_asm_template2(cmp);                     // 打印汇编指令，例如："cmp $5, %eax"
  // 注意：cmp 不写回结果，所以没有 operand_write
}

// inc 指令：自增 1
// 语义：dest = dest + 1，更新 EFLAGS（除了 CF）
// 特点：inc 不影响 CF（这是 x86 的特殊规定，与 add 不同）
// 用途：循环计数、指针移动等
make_EHelper(inc) {
  rtl_get_CF(&t3);                              // 先保存 CF 的当前值（因为 inc 不能改变 CF）
  rtl_addi(&t2, &id_dest->val, 1);             // t2 = dest + 1（rtl_addi = add immediate）
  operand_write(id_dest, &t2);                  // 写回结果

  rtl_update_ZFSF(&t2, id_dest->width);         // 更新 ZF 和 SF

  // 计算 OF：正数加 1 变成负数（最高位翻转）就是溢出
  // 判断方法：dest 的最高位为 0，result 的最高位为 1
  rtl_xor(&t0, &id_dest->val, &t2);            // t0 = dest ^ result（找出变化的位）
  rtl_msb(&t1, &id_dest->val, id_dest->width); // t1 = dest 的最高位
  rtl_not(&t1);                                 // t1 = ~t1（dest 最高位为 0 时 t1=1）
  rtl_shli(&t1, &t1, id_dest->width * 8 - 1); // 把 t1 移到最高位位置
  rtl_and(&t0, &t0, &t1);                      // t0 = 变化的位 & (dest 最高位为 0)
  rtl_msb(&t0, &t0, id_dest->width);           // t0 = t0 的最高位
  rtl_set_OF(&t0);                              // OF = t0
  rtl_set_CF(&t3);                              // 恢复 CF（inc 不改变 CF）

  // Log("pa2-debug: inc result=0x%08x", t2);

  print_asm_template1(inc);                     // 单操作数指令，例如："inc %eax"
}

// dec 指令：自减 1
// 语义：dest = dest - 1，更新 EFLAGS（除了 CF）
// 特点：dec 不影响 CF（与 sub 不同）
make_EHelper(dec) {
  rtl_get_CF(&t3);                              // 先保存 CF
  rtl_subi(&t2, &id_dest->val, 1);             // t2 = dest - 1
  operand_write(id_dest, &t2);                  // 写回结果

  rtl_update_ZFSF(&t2, id_dest->width);         // 更新 ZF 和 SF

  // 计算 OF：负数减 1 变成正数就是溢出
  // 判断方法：dest 的最高位为 1，result 的最高位为 0
  rtl_xor(&t0, &id_dest->val, &t2);            // t0 = dest ^ result
  rtl_msb(&t1, &id_dest->val, id_dest->width); // t1 = dest 的最高位
  rtl_shli(&t1, &t1, id_dest->width * 8 - 1); // 把 t1 移到最高位
  rtl_and(&t0, &t0, &t1);                      // t0 = 变化的位 & (dest 最高位为 1)
  rtl_msb(&t0, &t0, id_dest->width);
  rtl_set_OF(&t0);
  rtl_set_CF(&t3);                              // 恢复 CF

  // Log("pa2-debug: dec result=0x%08x", t2);

  print_asm_template1(dec);
}

// neg 指令：取负（求补码）
// 语义：dest = -dest = 0 - dest，更新 EFLAGS
// 用途：把正数变负数，负数变正数
make_EHelper(neg) {
  rtl_li(&t0, 0);                               // t0 = 0
  rtl_sub(&t2, &t0, &id_dest->val);            // t2 = 0 - dest（相当于取负）
  operand_write(id_dest, &t2);                  // 写回结果

  rtl_update_ZFSF(&t2, id_dest->width);         // 更新 ZF 和 SF

  // CF：如果 dest 非 0，CF=1；如果 dest=0，CF=0
  // （因为 0 - 非零数需要借位）
  rtl_neq0(&t0, &id_dest->val);                // t0 = (dest != 0) ? 1 : 0
  rtl_set_CF(&t0);

  // 计算 OF：只有一种情况会溢出：对最小负数取负
  // 例如 8 位：-128 取负还是 -128（因为没有 +128）
  rtl_xor(&t0, &id_dest->val, &t2);            // t0 = dest ^ result
  rtl_neq0(&t1, &id_dest->val);                // t1 = (dest != 0) ? 1 : 0
  rtl_shli(&t1, &t1, id_dest->width * 8 - 1); // 把 t1 移到最高位
  rtl_and(&t0, &t0, &t1);
  rtl_msb(&t0, &t0, id_dest->width);
  rtl_set_OF(&t0);

  // Log("pa2-debug: neg result=0x%08x", t2);

  print_asm_template1(neg);
}

// adc 指令：带进位加法（Add with Carry）
// 语义：dest = dest + src + CF，更新 EFLAGS
// 用途：多精度加法（例如实现 64 位加法：先 add 低 32 位，再 adc 高 32 位）
make_EHelper(adc) {
  rtl_add(&t2, &id_dest->val, &id_src->val);   // t2 = dest + src
  rtl_sltu(&t3, &t2, &id_dest->val);            // t3 = (t2 < dest) ? 1 : 0（第一次进位）
  rtl_get_CF(&t1);                              // t1 = 当前的 CF 值
  rtl_add(&t2, &t2, &t1);                      // t2 = t2 + CF（加上进位）
  operand_write(id_dest, &t2);                  // 写回结果

  rtl_update_ZFSF(&t2, id_dest->width);         // 更新 ZF 和 SF

  // 计算新的 CF：只要两次加法中有一次产生进位，CF 就置 1
  rtl_sltu(&t0, &t2, &id_dest->val);            // t0 = (t2 < dest) ? 1 : 0（第二次进位）
  rtl_or(&t0, &t3, &t0);                       // CF = 第一次进位 | 第二次进位
  rtl_set_CF(&t0);

  // 计算 OF（与 add 相同）
  rtl_xor(&t0, &id_dest->val, &id_src->val);
  rtl_not(&t0);
  rtl_xor(&t1, &id_dest->val, &t2);
  rtl_and(&t0, &t0, &t1);
  rtl_msb(&t0, &t0, id_dest->width);
  rtl_set_OF(&t0);

  print_asm_template2(adc);
}

// sbb 指令：带借位减法（Subtract with Borrow）
// 语义：dest = dest - src - CF，更新 EFLAGS
// 用途：多精度减法（例如实现 64 位减法：先 sub 低 32 位，再 sbb 高 32 位）
make_EHelper(sbb) {
  rtl_sub(&t2, &id_dest->val, &id_src->val);   // t2 = dest - src
  rtl_sltu(&t3, &id_dest->val, &t2);            // t3 = (dest < t2) ? 1 : 0（第一次借位）
  rtl_get_CF(&t1);                              // t1 = 当前的 CF 值（借位标志）
  rtl_sub(&t2, &t2, &t1);                      // t2 = t2 - CF（减去借位）
  operand_write(id_dest, &t2);                  // 写回结果

  rtl_update_ZFSF(&t2, id_dest->width);         // 更新 ZF 和 SF

  // 计算新的 CF：只要两次减法中有一次需要借位，CF 就置 1
  rtl_sltu(&t0, &id_dest->val, &t2);
  rtl_or(&t0, &t3, &t0);
  rtl_set_CF(&t0);

  // 计算 OF（与 sub 相同）
  rtl_xor(&t0, &id_dest->val, &id_src->val);
  rtl_xor(&t1, &id_dest->val, &t2);
  rtl_and(&t0, &t0, &t1);
  rtl_msb(&t0, &t0, id_dest->width);
  rtl_set_OF(&t0);

  print_asm_template2(sbb);
}

// mul 指令：无符号乘法
// 语义：根据操作数宽度，执行不同的乘法
//   8 位：AX = AL * src
//   16 位：DX:AX = AX * src（高 16 位在 DX，低 16 位在 AX）
//   32 位：EDX:EAX = EAX * src（高 32 位在 EDX，低 32 位在 EAX）
// 用途：无符号数乘法
make_EHelper(mul) {
  rtl_lr(&t0, R_EAX, id_dest->width);           // t0 = EAX 的低 width 字节（乘数之一）
  rtl_mul(&t0, &t1, &id_dest->val, &t0);       // {t0, t1} = dest * t0（64 位结果）
                                                // t0 = 高位，t1 = 低位

  switch (id_dest->width) {
    case 1:                                     // 8 位乘法：AX = AL * src
      rtl_sr_w(R_AX, &t1);                      // AX = 低 16 位结果
      break;
    case 2:                                     // 16 位乘法：DX:AX = AX * src
      rtl_sr_w(R_AX, &t1);                      // AX = 低 16 位
      rtl_shri(&t1, &t1, 16);                   // t1 右移 16 位得到高 16 位
      rtl_sr_w(R_DX, &t1);                      // DX = 高 16 位
      break;
    case 4:                                     // 32 位乘法：EDX:EAX = EAX * src
      rtl_sr_l(R_EDX, &t0);                     // EDX = 高 32 位
      rtl_sr_l(R_EAX, &t1);                     // EAX = 低 32 位
      break;
    default: assert(0);
  }

  print_asm_template1(mul);
}

// imul1 指令：有符号乘法（单操作数形式）
// 语义：与 mul 相同，但操作数被视为有符号数
make_EHelper(imul1) {
  rtl_lr(&t0, R_EAX, id_dest->width);
  rtl_imul(&t0, &t1, &id_dest->val, &t0);      // 有符号乘法

  switch (id_dest->width) {
    case 1:
      rtl_sr_w(R_AX, &t1);
      break;
    case 2:
      rtl_sr_w(R_AX, &t1);
      rtl_shri(&t1, &t1, 16);
      rtl_sr_w(R_DX, &t1);
      break;
    case 4:
      rtl_sr_l(R_EDX, &t0);
      rtl_sr_l(R_EAX, &t1);
      break;
    default: assert(0);
  }

  print_asm_template1(imul);
}

// imul2 指令：有符号乘法（双操作数形式）
// 语义：dest = dest * src（只保留低位，丢弃高位）
// 用途：常规的有符号乘法，结果不超过操作数宽度
make_EHelper(imul2) {
  rtl_sext(&id_src->val, &id_src->val, id_src->width);
  // rtl_sext = 符号扩展，把小宽度的有符号数扩展成大宽度
  rtl_sext(&id_dest->val, &id_dest->val, id_dest->width);

  rtl_imul(&t0, &t1, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t1);                  // 只写回低位（t1）

  print_asm_template2(imul);
}

// imul3 指令：有符号乘法（三操作数形式）
// 语义：dest = src2 * src（dest、src、src2 三个操作数）
// 例如：imul eax, ebx, 5  → eax = ebx * 5
make_EHelper(imul3) {
  rtl_sext(&id_src->val, &id_src->val, id_src->width);
  rtl_sext(&id_src2->val, &id_src2->val, id_src2->width);
  rtl_sext(&id_dest->val, &id_dest->val, id_dest->width);

  rtl_imul(&t0, &t1, &id_src2->val, &id_src->val);
  operand_write(id_dest, &t1);

  print_asm_template3(imul);                    // 三操作数指令
}

// div 指令：无符号除法
// 语义：根据操作数宽度执行不同的除法
//   8 位：AL = AX / src, AH = AX % src
//   16 位：AX = DX:AX / src, DX = DX:AX % src
//   32 位：EAX = EDX:EAX / src, EDX = EDX:EAX % src
// 用途：无符号数除法和取模
make_EHelper(div) {
  switch (id_dest->width) {
    case 1:                                     // 8 位除法：被除数在 AX
      rtl_li(&t1, 0);                           // 高位清零
      rtl_lr_w(&t0, R_AX);                      // t0 = AX（被除数）
      break;
    case 2:                                     // 16 位除法：被除数在 DX:AX
      rtl_lr_w(&t0, R_AX);                      // t0 = AX（低 16 位）
      rtl_lr_w(&t1, R_DX);                      // t1 = DX（高 16 位）
      rtl_shli(&t1, &t1, 16);                   // t1 左移 16 位
      rtl_or(&t0, &t0, &t1);                   // t0 = DX:AX（32 位被除数）
      rtl_li(&t1, 0);
      break;
    case 4:                                     // 32 位除法：被除数在 EDX:EAX
      rtl_lr_l(&t0, R_EAX);                     // t0 = EAX（低 32 位）
      rtl_lr_l(&t1, R_EDX);                     // t1 = EDX（高 32 位）
      break;
    default: assert(0);
  }

  rtl_div(&t2, &t3, &t1, &t0, &id_dest->val);  // {t2, t3} = {商, 余数} = {t1:t0} / dest

  rtl_sr(R_EAX, id_dest->width, &t2);           // EAX = 商
  if (id_dest->width == 1) {
    rtl_sr_b(R_AH, &t3);                        // AH = 余数（8 位除法特殊处理）
  }
  else {
    rtl_sr(R_EDX, id_dest->width, &t3);         // EDX = 余数
  }

  print_asm_template1(div);
}

// idiv 指令：有符号除法
// 语义：与 div 相同，但操作数被视为有符号数
make_EHelper(idiv) {
  rtl_sext(&id_dest->val, &id_dest->val, id_dest->width);
  // 先对除数进行符号扩展

  switch (id_dest->width) {
    case 1:
      rtl_lr_w(&t0, R_AX);
      rtl_sext(&t0, &t0, 2);                    // 把 AX 符号扩展到 32 位
      rtl_msb(&t1, &t0, 4);                     // 取符号位
      rtl_sub(&t1, &tzero, &t1);                // t1 = 0 - 符号位（得到高位全 1 或全 0）
      break;
    case 2:
      rtl_lr_w(&t0, R_AX);
      rtl_lr_w(&t1, R_DX);
      rtl_shli(&t1, &t1, 16);
      rtl_or(&t0, &t0, &t1);
      rtl_msb(&t1, &t0, 4);
      rtl_sub(&t1, &tzero, &t1);
      break;
    case 4:
      rtl_lr_l(&t0, R_EAX);
      rtl_lr_l(&t1, R_EDX);
      break;
    default: assert(0);
  }

  rtl_idiv(&t2, &t3, &t1, &t0, &id_dest->val);  // 有符号除法

  rtl_sr(R_EAX, id_dest->width, &t2);           // EAX = 商
  if (id_dest->width == 1) {
    rtl_sr_b(R_AH, &t3);                        // AH = 余数
  }
  else {
    rtl_sr(R_EDX, id_dest->width, &t3);         // EDX = 余数
  }

  print_asm_template1(idiv);
}
