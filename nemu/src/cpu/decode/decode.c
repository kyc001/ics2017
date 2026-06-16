// decode.c - 指令译码（PA2 核心文件）
// 功能：解析 x86 指令的操作数（寄存器、内存地址、立即数等）
// 这是指令执行的第二阶段：取指令 → **译码** → 执行

#include "cpu/exec.h"     // 指令执行框架
#include "cpu/rtl.h"      // RTL 层接口

/* shared by all helper functions */
// 全局变量：所有译码和执行函数共享

DecodeInfo decoding;      // 译码信息结构体（存放当前指令的所有信息）
                          // 包括：操作码、操作数、指令长度、跳转目标等
rtlreg_t t0, t1, t2, t3;  // 临时寄存器（执行函数中用来存放中间结果）
                          // rtlreg_t = RTL 寄存器类型（实际是 uint32_t）
const rtlreg_t tzero = 0; // 常量 0（经常用到，定义成全局变量避免重复赋值）

#define make_DopHelper(name) void concat(decode_op_, name) (vaddr_t *eip, Operand *op, bool load_val)
// 宏：定义译码辅助函数的签名
// 例如：make_DopHelper(I) 展开成 void decode_op_I(vaddr_t *eip, Operand *op, bool load_val)
// 参数：eip = 指向当前解析位置的指针（会被更新）
//       op = 指向操作数结构体的指针（填充解析结果）
//       load_val = 是否需要立即读取操作数的值（false 表示只解析地址，不读值）

/* Refer to Appendix A in i386 manual for the explanations of these abbreviations */
// 参考 i386 手册附录 A，了解这些缩写的含义
// 例如：I = Immediate（立即数），r = register（寄存器），m = memory（内存）

/* Ib, Iv */
// decode_op_I: 解析立即数操作数（Immediate）
// Ib = 8 位立即数，Iv = 16/32 位立即数（根据操作数大小前缀）
// 例如：mov $100, %eax 中的 $100 就是立即数
static inline make_DopHelper(I) {
  /* eip here is pointing to the immediate */
  // eip 指向立即数的起始位置
  op->type = OP_TYPE_IMM;               // 操作数类型 = 立即数
  op->imm = instr_fetch(eip, op->width);// 从指令流中读取 op->width 字节
                                        // instr_fetch 会自动更新 eip（前进 op->width 字节）
  rtl_li(&op->val, op->imm);            // 把立即数加载到 op->val

#ifdef DEBUG
  snprintf(op->str, OP_STR_SIZE, "$0x%x", op->imm);
  // 调试模式：格式化操作数字符串，例如 "$0x64"（十六进制 100）
#endif
}

/* I386 manual does not contain this abbreviation, but it is different from
 * the one above from the view of implementation. So we use another helper
 * function to decode it.
 */
/* sign immediate */
// decode_op_SI: 解析有符号立即数（Sign-extended Immediate）
// 与 decode_op_I 的区别：SI 会进行符号扩展
// 例如：8 位的 -1（0xFF）符号扩展到 32 位变成 0xFFFFFFFF
static inline make_DopHelper(SI) {
  assert(op->width == 1 || op->width == 2 || op->width == 4);

  op->type = OP_TYPE_IMM;               // 操作数类型 = 立即数

  /* TODO: Use instr_fetch() to read `op->width' bytes of memory
   * pointed by `eip'. Interpret the result as a signed immediate,
   * and assign it to op->simm.
   *
   op->simm = ???
   */
  // 从指令流读取有符号立即数，并进行符号扩展
  switch (op->width) {
    case 1: op->simm = (int8_t)instr_fetch(eip, 1); break;
    // 读 1 字节，强制转换成 int8_t（有符号），再赋值给 int32_t 类型的 simm
    // 这样会自动符号扩展：0xFF(8位) → 0xFFFFFFFF(32位)
    case 2: op->simm = (int16_t)instr_fetch(eip, 2); break;
    // 读 2 字节，符号扩展到 32 位
    case 4: op->simm = (int32_t)instr_fetch(eip, 4); break;
    // 读 4 字节，本身就是 32 位，不需要扩展
    default: assert(0);
  }

  rtl_li(&op->val, op->simm);           // 把有符号立即数加载到 op->val

#ifdef DEBUG
  snprintf(op->str, OP_STR_SIZE, "$0x%x", op->simm);
#endif
}

/* I386 manual does not contain this abbreviation.
 * It is convenient to merge them into a single helper function.
 */
/* AL/eAX */
// decode_op_a: 解析累加器寄存器（AL/AX/EAX）
// 'a' = accumulator（累加器），即 EAX 寄存器
// 根据操作数宽度，可能是 AL（8位）、AX（16位）或 EAX（32位）
static inline make_DopHelper(a) {
  op->type = OP_TYPE_REG;               // 操作数类型 = 寄存器
  op->reg = R_EAX;                      // 寄存器编号 = 0（EAX）
  if (load_val) {                       // 如果需要立即读取值
    op->val = decode_reg_read(R_EAX, op->width);
    // 从 EAX 读取 op->width 字节
  }

#ifdef DEBUG
  snprintf(op->str, OP_STR_SIZE, "%%%s", reg_name(R_EAX, op->width));
  // 格式化：例如 "%eax"、"%ax"、"%al"
#endif
}

/* This helper function is use to decode register encoded in the opcode. */
/* XX: AL, AH, BL, BH, CL, CH, DL, DH
 * eXX: eAX, eCX, eDX, eBX, eSP, eBP, eSI, eDI
 */
// decode_op_r: 解析编码在操作码中的寄存器
// 有些指令的寄存器编号直接编码在操作码的低 3 位
// 例如：0x50-0x57 是 push eax/ecx/.../edi，寄存器编号 = opcode & 0x7
static inline make_DopHelper(r) {
  op->type = OP_TYPE_REG;               // 操作数类型 = 寄存器
  op->reg = decoding.opcode & 0x7;      // 寄存器编号 = 操作码的低 3 位（0-7）
                                        // 0=EAX, 1=ECX, 2=EDX, 3=EBX, 4=ESP, 5=EBP, 6=ESI, 7=EDI
  if (load_val) {                       // 如果需要立即读取值
    op->val = decode_reg_read(op->reg, op->width);
  }

#ifdef DEBUG
  snprintf(op->str, OP_STR_SIZE, "%%%s", reg_name(op->reg, op->width));
#endif
}

/* I386 manual does not contain this abbreviation.
 * We decode everything of modR/M byte by one time.
 */
/* Eb, Ew, Ev
 * Gb, Gv
 * Cd,
 * M
 * Rd
 * Sw
 */
// decode_op_rm: 解析 ModR/M 字节（最复杂的寻址方式）
// ModR/M 字节包含两个操作数的信息：
//   - rm（R/M 字段）：可以是寄存器或内存
//   - reg（Reg 字段）：通常是寄存器
// 例如：mov %eax, (%ebx) 中，eax 在 reg 字段，(%ebx) 在 rm 字段
static inline void decode_op_rm(vaddr_t *eip, Operand *rm, bool load_rm_val, Operand *reg, bool load_reg_val) {
  read_ModR_M(eip, rm, load_rm_val, reg, load_reg_val);
  // 调用 read_ModR_M 函数解析 ModR/M 字节（实现在别处）
  // 这个函数会：
  //   1. 从 eip 读取 ModR/M 字节
  //   2. 解析出 mod、reg、rm 三个字段
  //   3. 根据 mod 和 rm 计算内存地址（可能还要读取 SIB 字节和位移）
  //   4. 填充 rm 和 reg 两个操作数结构体
}

/* Ob, Ov */
static inline make_DopHelper(O) {
  op->type = OP_TYPE_MEM;
  op->addr = instr_fetch(eip, 4);
  if (load_val) {
    rtl_lm(&op->val, &op->addr, op->width);
  }

#ifdef DEBUG
  snprintf(op->str, OP_STR_SIZE, "0x%x", op->addr);
#endif
}

/* Eb <- Gb
 * Ev <- Gv
 */
make_DHelper(G2E) {
  decode_op_rm(eip, id_dest, true, id_src, true);
}

make_DHelper(mov_G2E) {
  decode_op_rm(eip, id_dest, false, id_src, true);
}

/* Gb <- Eb
 * Gv <- Ev
 */
make_DHelper(E2G) {
  decode_op_rm(eip, id_src, true, id_dest, true);
}

make_DHelper(mov_E2G) {
  decode_op_rm(eip, id_src, true, id_dest, false);
}

make_DHelper(mov_r2CR) {
  decode_op_rm(eip, id_src, true, id_dest, false);
}

make_DHelper(mov_CR2r) {
  decode_op_rm(eip, id_dest, false, id_src, false);
}

make_DHelper(lea_M2G) {
  decode_op_rm(eip, id_src, false, id_dest, false);
}

/* AL <- Ib
 * eAX <- Iv
 */
make_DHelper(I2a) {
  decode_op_a(eip, id_dest, true);
  decode_op_I(eip, id_src, true);
}

/* Gv <- EvIb
 * Gv <- EvIv
 * use for imul */
make_DHelper(I_E2G) {
  decode_op_rm(eip, id_src2, true, id_dest, false);
  decode_op_I(eip, id_src, true);
}

/* Eb <- Ib
 * Ev <- Iv
 */
make_DHelper(I2E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
  decode_op_I(eip, id_src, true);
}

make_DHelper(mov_I2E) {
  decode_op_rm(eip, id_dest, false, NULL, false);
  decode_op_I(eip, id_src, true);
}

/* XX <- Ib
 * eXX <- Iv
 */
make_DHelper(I2r) {
  decode_op_r(eip, id_dest, true);
  decode_op_I(eip, id_src, true);
}

make_DHelper(mov_I2r) {
  decode_op_r(eip, id_dest, false);
  decode_op_I(eip, id_src, true);
}

/* used by unary operations */
make_DHelper(I) {
  decode_op_I(eip, id_dest, true);
}

make_DHelper(r) {
  decode_op_r(eip, id_dest, true);
}

make_DHelper(E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
}

make_DHelper(setcc_E) {
  decode_op_rm(eip, id_dest, false, NULL, false);
}

make_DHelper(gp7_E) {
  decode_op_rm(eip, id_dest, false, NULL, false);
}

/* used by test in group3 */
make_DHelper(test_I) {
  decode_op_I(eip, id_src, true);
}

make_DHelper(SI2E) {
  assert(id_dest->width == 2 || id_dest->width == 4);
  decode_op_rm(eip, id_dest, true, NULL, false);
  id_src->width = 1;
  decode_op_SI(eip, id_src, true);
  if (id_dest->width == 2) {
    id_src->val &= 0xffff;
  }
}

make_DHelper(SI_E2G) {
  assert(id_dest->width == 2 || id_dest->width == 4);
  decode_op_rm(eip, id_src2, true, id_dest, false);
  id_src->width = 1;
  decode_op_SI(eip, id_src, true);
  if (id_dest->width == 2) {
    id_src->val &= 0xffff;
  }
}

make_DHelper(gp2_1_E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
  id_src->type = OP_TYPE_IMM;
  id_src->imm = 1;
  rtl_li(&id_src->val, 1);
#ifdef DEBUG
  sprintf(id_src->str, "$1");
#endif
}

make_DHelper(gp2_cl2E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
  id_src->type = OP_TYPE_REG;
  id_src->reg = R_CL;
  rtl_lr_b(&id_src->val, R_CL);
#ifdef DEBUG
  sprintf(id_src->str, "%%cl");
#endif
}

make_DHelper(gp2_Ib2E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
  id_src->width = 1;
  decode_op_I(eip, id_src, true);
}

/* Ev <- GvIb
 * use for shld/shrd */
make_DHelper(Ib_G2E) {
  decode_op_rm(eip, id_dest, true, id_src2, true);
  id_src->width = 1;
  decode_op_I(eip, id_src, true);
}

make_DHelper(cl_G2E) {
  decode_op_rm(eip, id_dest, true, id_src2, true);
  id_src->type = OP_TYPE_REG;
  id_src->reg = R_CL;
  rtl_lr_b(&id_src->val, R_CL);
#ifdef DEBUG
  sprintf(id_src->str, "%%cl");
#endif
}

make_DHelper(O2a) {
  decode_op_O(eip, id_src, true);
  decode_op_a(eip, id_dest, false);
}

make_DHelper(a2O) {
  decode_op_a(eip, id_src, true);
  decode_op_O(eip, id_dest, false);
}

make_DHelper(J) {
  id_dest->type = OP_TYPE_IMM;
  switch (id_dest->width) {
    case 1: id_dest->simm = (int8_t)instr_fetch(eip, 1); break;
    case 2: id_dest->simm = (int16_t)instr_fetch(eip, 2); break;
    case 4: id_dest->simm = (int32_t)instr_fetch(eip, 4); break;
    default: assert(0);
  }
  id_dest->val = id_dest->simm;
  // the target address can be computed in the decode stage
  decoding.jmp_eip = id_dest->simm + *eip;
#ifdef DEBUG
  snprintf(id_dest->str, OP_STR_SIZE, "$0x%x", id_dest->simm);
#endif
}

make_DHelper(push_SI) {
  decode_op_SI(eip, id_dest, true);
}

make_DHelper(in_I2a) {
  id_src->width = 1;
  decode_op_I(eip, id_src, true);
  decode_op_a(eip, id_dest, false);
}

make_DHelper(in_dx2a) {
  id_src->type = OP_TYPE_REG;
  id_src->reg = R_DX;
  rtl_lr_w(&id_src->val, R_DX);
#ifdef DEBUG
  sprintf(id_src->str, "(%%dx)");
#endif

  decode_op_a(eip, id_dest, false);
}

make_DHelper(out_a2I) {
  decode_op_a(eip, id_src, true);
  id_dest->width = 1;
  decode_op_I(eip, id_dest, true);
}

make_DHelper(out_a2dx) {
  decode_op_a(eip, id_src, true);

  id_dest->type = OP_TYPE_REG;
  id_dest->reg = R_DX;
  id_dest->val = cpu.gpr[R_DX]._16;
#ifdef DEBUG
  sprintf(id_dest->str, "(%%dx)");
#endif
}
