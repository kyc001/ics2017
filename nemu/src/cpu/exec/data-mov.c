// data-mov.c - 数据传送指令的执行函数（PA2 核心文件之一）
// 功能：实现 x86 的数据传送指令（mov、push、pop、lea 等）
// 这些指令负责在寄存器、内存之间移动数据，是最常用的指令类型

#include "cpu/exec.h"     // 指令执行框架的头文件

#ifdef HAS_IOE
extern bool device_update_in_instr(void);
// 如果编译时开启了 IOE（I/O Extension），需要在字符串指令中轮询设备
#endif

#define REP_DEVICE_POLL_MASK 0x7f
// 设备轮询掩码：每执行 128 次（0x7f+1）rep 循环，检查一次设备
// 防止 rep 指令执行太久导致设备响应延迟

// mov 指令：数据传送（最基本、最常用的指令）
// 语义：dest = src
// 用途：在寄存器、内存之间复制数据
// 例如：mov eax, ebx（寄存器→寄存器）
//       mov eax, [0x100]（内存→寄存器）
//       mov [0x100], eax（寄存器→内存）
make_EHelper(mov) {
  operand_write(id_dest, &id_src->val);         // 把源操作数的值写到目的操作数
                                                // operand_write 会根据目的操作数类型选择写入方式
  print_asm_template2(mov);                     // 打印汇编指令，例如："mov %eax, %ebx"
}

// movs 指令：字符串传送（Move String）
// 语义：把 [esi] 的数据复制到 [edi]，然后 esi 和 edi 都增加 width
// 可以带 rep 前缀：rep movs 表示重复 ecx 次
// 用途：内存块复制（类似 memcpy）
// 例如：rep movsd（复制 ecx 个双字，从 [esi] 到 [edi]）
make_EHelper(movs) {
  uint32_t count = decoding.rep_prefix ? cpu.ecx : 1;
  // 如果有 rep 前缀，重复次数 = ecx；否则只执行 1 次
#ifdef HAS_IOE
  uint32_t budget = 0;                          // 设备轮询计数器
#endif
  while (count > 0) {                           // 循环复制
    rtl_li(&t0, vaddr_read(cpu.esi, decoding.dest.width));
    // 从 [esi] 读取 width 字节到 t0
    vaddr_write(cpu.edi, decoding.dest.width, t0);
    // 把 t0 写到 [edi]
    cpu.esi += decoding.dest.width;             // esi 前进（复制源地址递增）
    cpu.edi += decoding.dest.width;             // edi 前进（复制目标地址递增）
    count --;                                   // 剩余次数 -1
#ifdef HAS_IOE
    if (decoding.rep_prefix && (++budget & REP_DEVICE_POLL_MASK) == 0) {
      // 每 128 次循环，检查一次设备（键盘、鼠标等）
      if (device_update_in_instr()) {
        // 如果有设备事件需要处理
        cpu.ecx = count;                        // 保存剩余次数到 ecx
        return;                                 // 暂停 rep，让设备中断处理
      }
    }
#endif
  }
  if (decoding.rep_prefix) {
    cpu.ecx = 0;                                // rep 执行完毕，ecx 清零
  }

  if (decoding.dest.width == 1) {
    print_asm(decoding.rep_prefix ? "rep movsb" : "movsb");
    // movsb = 按字节复制
  }
  else {
    print_asm(decoding.rep_prefix ? "rep movsd" : "movsd");
    // movsd = 按双字（4 字节）复制
  }
}

// push 指令：压栈
// 语义：esp -= width; [esp] = src
// 用途：保存数据到栈上（函数调用、保存寄存器等）
// 栈从高地址向低地址增长，所以 push 会让 esp 减小
make_EHelper(push) {
  rtl_push(&id_dest->val);                      // 把目的操作数的值压栈
                                                // rtl_push 内部会：esp -= width; mem[esp] = val

  print_asm_template1(push);                    // 打印汇编指令，例如："push %eax"
}

// pop 指令：出栈
// 语义：dest = [esp]; esp += width
// 用途：从栈上恢复数据
make_EHelper(pop) {
  rtl_pop(&t0);                                 // 从栈顶弹出值到 t0
                                                // rtl_pop 内部会：val = mem[esp]; esp += width
  operand_write(id_dest, &t0);                  // 把弹出的值写到目的操作数

  print_asm_template1(pop);                     // 打印汇编指令，例如："pop %eax"
}

// pusha 指令：压入所有通用寄存器
// 语义：按顺序压入 eax、ecx、edx、ebx、esp（原值）、ebp、esi、edi
// 用途：函数入口保存现场（一次保存所有寄存器）
// 注意：压入的 esp 是执行 pusha 之前的值
make_EHelper(pusha) {
  rtl_mv(&t0, &cpu.esp);                        // t0 = esp 的原值（要先保存，因为后面 push 会改变 esp）
  rtl_push(&cpu.eax);                           // 压入 eax
  rtl_push(&cpu.ecx);                           // 压入 ecx
  rtl_push(&cpu.edx);                           // 压入 edx
  rtl_push(&cpu.ebx);                           // 压入 ebx
  rtl_push(&t0);                                // 压入 esp 的原值（不是当前的 esp）
  rtl_push(&cpu.ebp);                           // 压入 ebp
  rtl_push(&cpu.esi);                           // 压入 esi
  rtl_push(&cpu.edi);                           // 压入 edi

  print_asm("pusha");                           // 打印汇编指令："pusha"
}

// popa 指令：弹出所有通用寄存器
// 语义：按相反顺序弹出 edi、esi、ebp、(跳过 esp)、ebx、edx、ecx、eax
// 用途：函数出口恢复现场
// 注意：弹出的第 4 个值被丢弃（不恢复到 esp），因为 esp 已经被 pop 操作自动更新了
make_EHelper(popa) {
  rtl_pop(&cpu.edi);                            // 弹出 edi
  rtl_pop(&cpu.esi);                            // 弹出 esi
  rtl_pop(&cpu.ebp);                            // 弹出 ebp
  rtl_pop(&t0);                                 // 弹出一个值到 t0（丢弃，不恢复到 esp）
  rtl_pop(&cpu.ebx);                            // 弹出 ebx
  rtl_pop(&cpu.edx);                            // 弹出 edx
  rtl_pop(&cpu.ecx);                            // 弹出 ecx
  rtl_pop(&cpu.eax);                            // 弹出 eax

  print_asm("popa");
}

// leave 指令：释放栈帧（函数尾声的标准操作）
// 语义：esp = ebp; pop ebp
// 用途：与函数开头的 "push ebp; mov ebp, esp" 配对
//       快速恢复调用者的栈帧
// 等价于：mov esp, ebp（恢复栈顶）
//         pop ebp（恢复旧的栈底）
make_EHelper(leave) {
  rtl_mv(&cpu.esp, &cpu.ebp);                   // esp = ebp（丢弃当前函数的局部变量）
  rtl_pop(&cpu.ebp);                            // 弹出旧的 ebp（恢复调用者的栈底）

  print_asm("leave");
}

// cltd 指令：符号扩展（Convert Long To Double）
// 语义：把 eax 的符号位扩展到 edx（为有符号除法做准备）
//       如果 eax 是负数，edx = 0xFFFFFFFF
//       如果 eax 是正数，edx = 0x00000000
// 用途：在执行 idiv 之前，把 32 位被除数扩展成 64 位
// 例如：idiv 指令要求被除数在 edx:eax，cltd 把 eax 符号扩展到 edx
make_EHelper(cltd) {
  if (decoding.is_operand_size_16) {            // 16 位模式：cwd（Convert Word to Double）
    rtl_lr(&t0, R_AX, 2);                       // t0 = AX（16 位）
    rtl_msb(&t1, &t0, 2);                       // t1 = AX 的最高位（符号位）
    rtl_li(&t0, t1 ? 0xffff : 0);               // 如果符号位=1，t0=0xFFFF；否则 t0=0
    rtl_sr(R_DX, 2, &t0);                       // DX = t0（符号扩展结果）
  }
  else {                                        // 32 位模式：cltd（Convert Long to Double）
    rtl_msb(&t0, &cpu.eax, 4);                  // t0 = EAX 的最高位
    rtl_li(&cpu.edx, t0 ? 0xffffffffu : 0);     // 如果符号位=1，EDX=0xFFFFFFFF；否则 EDX=0
  }

  print_asm(decoding.is_operand_size_16 ? "cwtl" : "cltd");
  // 打印的汇编名称可能不准确，实际应该是 cwd/cdq
}

// cwtl 指令：符号扩展（Convert Word To Long）
// 语义：把 ax 符号扩展到 eax（16 位扩展到 32 位）
//       或把 al 符号扩展到 ax（8 位扩展到 16 位）
// 用途：把小宽度的有符号数转换成大宽度
make_EHelper(cwtl) {
  if (decoding.is_operand_size_16) {            // 16 位模式：cbtw（Convert Byte to Word）
    rtl_lr(&t0, R_AL, 1);                       // t0 = AL（8 位）
    rtl_sext(&t1, &t0, 1);                      // t1 = AL 符号扩展到 32 位
    rtl_sr(R_AX, 2, &t1);                       // AX = t1 的低 16 位（符号扩展结果）
  }
  else {                                        // 32 位模式：cwtl（Convert Word to Long）
    rtl_lr(&t0, R_AX, 2);                       // t0 = AX（16 位）
    rtl_sext(&cpu.eax, &t0, 2);                 // EAX = AX 符号扩展到 32 位
  }

  print_asm(decoding.is_operand_size_16 ? "cbtw" : "cwtl");
}

// movsx 指令：带符号扩展的传送（Move with Sign Extend）
// 语义：把小宽度的有符号数扩展到大宽度，然后传送
// 例如：movsx eax, bl（把 bl 的 8 位有符号数扩展到 32 位存入 eax）
// 用途：把 char 或 short 类型的有符号数转换成 int
make_EHelper(movsx) {
  id_dest->width = decoding.is_operand_size_16 ? 2 : 4;
  // 目的操作数宽度 = 2 字节（16 位模式）或 4 字节（32 位模式）
  rtl_sext(&t2, &id_src->val, id_src->width);  // t2 = src 符号扩展到 32 位
  operand_write(id_dest, &t2);                  // dest = t2
  print_asm_template2(movsx);
}

// movzx 指令：带零扩展的传送（Move with Zero Extend）
// 语义：把小宽度的无符号数扩展到大宽度，然后传送（高位补 0）
// 例如：movzx eax, bl（把 bl 的 8 位无符号数零扩展到 32 位存入 eax）
// 用途：把 unsigned char 或 unsigned short 转换成 unsigned int
make_EHelper(movzx) {
  id_dest->width = decoding.is_operand_size_16 ? 2 : 4;
  // 目的操作数宽度 = 2 或 4 字节
  operand_write(id_dest, &id_src->val);         // dest = src（高位自动补 0）
                                                // 因为 operand_write 只写低位，高位被清零
  print_asm_template2(movzx);
}

// lea 指令：加载有效地址（Load Effective Address）
// 语义：dest = src 的地址（而不是 src 的值）
// 例如：lea eax, [ebx+ecx*4+8]
//       → eax = ebx + ecx*4 + 8（计算地址，但不访问内存）
// 用途：① 快速计算地址（避免访问内存）
//       ② 巧妙地做算术运算（例如 lea eax, [eax+eax*2] 相当于 eax *= 3）
make_EHelper(lea) {
  rtl_li(&t2, id_src->addr);                    // t2 = src 的地址（不是 src 的值！）
                                                // id_src->addr 是译码阶段计算出的地址
  operand_write(id_dest, &t2);                  // dest = t2（地址值）
  print_asm_template2(lea);
}

// stos 指令：存储字符串（Store String）
// 语义：[edi] = eax; edi += width
// 可以带 rep 前缀：rep stos 表示重复 ecx 次
// 用途：内存块初始化（类似 memset）
// 例如：rep stosd（把 ecx 个双字都设为 eax 的值，存到 [edi] 开始的内存）
make_EHelper(stos) {
  uint32_t count = decoding.rep_prefix ? cpu.ecx : 1;
  // 如果有 rep 前缀，重复次数 = ecx；否则只执行 1 次
#ifdef HAS_IOE
  uint32_t budget = 0;                          // 设备轮询计数器
#endif
  while (count > 0) {                           // 循环填充
    vaddr_write(cpu.edi, decoding.dest.width, cpu.eax);
    // 把 eax 的低 width 字节写到 [edi]
    cpu.edi += decoding.dest.width;             // edi 前进
    count --;
#ifdef HAS_IOE
    if (decoding.rep_prefix && (++budget & REP_DEVICE_POLL_MASK) == 0) {
      // 每 128 次循环，检查一次设备
      if (device_update_in_instr()) {
        cpu.ecx = count;                        // 保存剩余次数
        return;                                 // 暂停 rep，让设备中断处理
      }
    }
#endif
  }
  if (decoding.rep_prefix) {
    cpu.ecx = 0;                                // rep 执行完毕，ecx 清零
  }
  print_asm(decoding.dest.width == 1 ?
      (decoding.rep_prefix ? "rep stosb" : "stosb") :
      (decoding.rep_prefix ? "rep stosd" : "stos"));
  // stosb = 按字节填充
  // stosd = 按双字（4 字节）填充
}
