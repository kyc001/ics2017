#ifndef __CPU_DECODE_H__
#define __CPU_DECODE_H__

#include "common.h"

#include "rtl.h"

enum { OP_TYPE_REG, OP_TYPE_MEM, OP_TYPE_IMM };

#define OP_STR_SIZE 40

typedef struct {
  uint32_t type;
  int width;
  union {
    uint32_t reg;
    rtlreg_t addr;
    uint32_t imm;
    int32_t simm;
  };
  rtlreg_t val;
  char str[OP_STR_SIZE];
} Operand;

typedef struct {
  uint32_t opcode;
  vaddr_t seq_eip;  // sequential eip
  bool is_operand_size_16;
  bool rep_prefix;
  uint8_t ext_opcode;
  bool is_jmp;
  vaddr_t jmp_eip;
  Operand src, dest, src2;
#ifdef DEBUG
  char assembly[80];
  char asm_buf[128];
  char *p;
#endif
} DecodeInfo;

typedef union {
  struct {
    uint8_t R_M		:3;
    uint8_t reg		:3;
    uint8_t mod		:2;
  };
  struct {
    uint8_t dont_care	:3;
    uint8_t opcode		:3;
  };
  uint8_t val;
} ModR_M;

typedef union {
  struct {
    uint8_t base	:3;
    uint8_t index	:3;
    uint8_t ss		:2;
  };
  uint8_t val;
} SIB;

/* shared by all helper functions */
extern DecodeInfo decoding;

static inline rtlreg_t decode_reg_read(int reg, int width);
static inline void decode_reg_write(int reg, int width, rtlreg_t val);

static inline uint32_t decode_fetch(vaddr_t *eip, int len) {
  uint32_t instr = vaddr_read(*eip, len);
#ifdef DEBUG
  uint8_t *p_instr = (void *)&instr;
  int i;
  for (i = 0; i < len; i ++) {
    decoding.p += sprintf(decoding.p, "%02x ", p_instr[i]);
  }
#endif
  *eip += len;
  return instr;
}

static inline void load_addr(vaddr_t *eip, ModR_M *m, Operand *rm) {
  assert(m->mod != 3);

  int32_t disp = 0;
  int disp_size = 4;
  int base_reg = -1, index_reg = -1, scale = 0;
  uint32_t addr = 0;

  if (m->R_M == R_ESP) {
    SIB s;
    s.val = decode_fetch(eip, 1);
    base_reg = s.base;
    scale = s.ss;
    if (s.index != R_ESP) { index_reg = s.index; }
  } else {
    base_reg = m->R_M;
  }

  if (m->mod == 0) {
    if (base_reg == R_EBP) { base_reg = -1; }
    else { disp_size = 0; }
  } else if (m->mod == 1) {
    disp_size = 1;
  }

  if (disp_size != 0) {
    disp = decode_fetch(eip, disp_size);
    if (disp_size == 1) { disp = (int8_t)disp; }
    addr = (uint32_t)disp;
  }

  if (base_reg != -1) { addr += cpu.gpr[base_reg]._32; }
  if (index_reg != -1) { addr += cpu.gpr[index_reg]._32 << scale; }
  rm->addr = addr;

#ifdef DEBUG
  char disp_buf[16];
  char base_buf[8];
  char index_buf[8];

  if (disp_size != 0) {
    sprintf(disp_buf, "%s%#x", (disp < 0 ? "-" : ""), (disp < 0 ? -disp : disp));
  } else {
    disp_buf[0] = '\0';
  }

  if (base_reg == -1) { base_buf[0] = '\0'; }
  else { sprintf(base_buf, "%%%s", reg_name(base_reg, 4)); }

  if (index_reg == -1) { index_buf[0] = '\0'; }
  else { sprintf(index_buf, ",%%%s,%d", reg_name(index_reg, 4), 1 << scale); }

  if (base_reg == -1 && index_reg == -1) { sprintf(rm->str, "%s", disp_buf); }
  else { sprintf(rm->str, "%s(%s%s)", disp_buf, base_buf, index_buf); }
#endif

  rm->type = OP_TYPE_MEM;
}

static inline void read_ModR_M(vaddr_t *eip, Operand *rm, bool load_rm_val, Operand *reg, bool load_reg_val) {
  ModR_M m;
  m.val = decode_fetch(eip, 1);
  decoding.ext_opcode = m.opcode;

  if (reg != NULL) {
    reg->type = OP_TYPE_REG;
    reg->reg = m.reg;
    if (load_reg_val) {
      reg->val = decode_reg_read(reg->reg, reg->width);
    }
#ifdef DEBUG
    snprintf(reg->str, OP_STR_SIZE, "%%%s", reg_name(reg->reg, reg->width));
#endif
  }

  if (m.mod == 3) {
    rm->type = OP_TYPE_REG;
    rm->reg = m.R_M;
    if (load_rm_val) {
      rm->val = decode_reg_read(m.R_M, rm->width);
    }
#ifdef DEBUG
    sprintf(rm->str, "%%%s", reg_name(m.R_M, rm->width));
#endif
    return;
  }

  load_addr(eip, &m, rm);
  if (load_rm_val) {
    rtl_lm(&rm->val, &rm->addr, rm->width);
  }
}

static inline rtlreg_t decode_reg_read(int reg, int width) {
  switch (width) {
    case 4: return cpu.gpr[reg]._32;
    case 2: return cpu.gpr[reg]._16;
    case 1: return cpu.gpr[reg & 0x3]._8[reg >> 2];
    default: assert(0); return 0;
  }
}

static inline void decode_reg_write(int reg, int width, rtlreg_t val) {
  switch (width) {
    case 4: cpu.gpr[reg]._32 = val; return;
    case 2: cpu.gpr[reg]._16 = val; return;
    case 1: cpu.gpr[reg & 0x3]._8[reg >> 2] = val; return;
    default: assert(0); return;
  }
}

static inline void operand_write(Operand *op, rtlreg_t *src) {
  if (op->type == OP_TYPE_REG) { decode_reg_write(op->reg, op->width, *src); }
  else if (op->type == OP_TYPE_MEM) { vaddr_write(op->addr, op->width, *src); }
  else { assert(0); }
}

#define id_src (&decoding.src)
#define id_src2 (&decoding.src2)
#define id_dest (&decoding.dest)

#define make_DHelper(name) void concat(decode_, name) (vaddr_t *eip)
typedef void (*DHelper) (vaddr_t *);

make_DHelper(I2E);
make_DHelper(I2a);
make_DHelper(I2r);
make_DHelper(SI2E);
make_DHelper(SI_E2G);
make_DHelper(I_E2G);
make_DHelper(I_G2E);
make_DHelper(I);
make_DHelper(r);
make_DHelper(E);
make_DHelper(setcc_E);
make_DHelper(gp7_E);
make_DHelper(test_I);
make_DHelper(SI);
make_DHelper(G2E);
make_DHelper(E2G);

make_DHelper(mov_I2r);
make_DHelper(mov_I2E);
make_DHelper(mov_G2E);
make_DHelper(mov_E2G);
make_DHelper(mov_r2CR);
make_DHelper(mov_CR2r);
make_DHelper(lea_M2G);

make_DHelper(gp2_1_E);
make_DHelper(gp2_cl2E);
make_DHelper(gp2_Ib2E);
make_DHelper(Ib_G2E);
make_DHelper(cl_G2E);

make_DHelper(O2a);
make_DHelper(a2O);

make_DHelper(J);

make_DHelper(push_SI);

make_DHelper(in_I2a);
make_DHelper(in_dx2a);
make_DHelper(out_a2I);
make_DHelper(out_a2dx);

#endif
