#include "cpu/exec.h"

make_EHelper(test) {
  rtl_and(&t2, &id_dest->val, &id_src->val);
  rtl_update_ZFSF(&t2, id_dest->width);
  rtl_li(&t0, 0);
  rtl_set_CF(&t0);
  rtl_set_OF(&t0);

  print_asm_template2(test);
}

make_EHelper(and) {
  rtl_and(&t2, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t2);

  rtl_update_ZFSF(&t2, id_dest->width);
  rtl_li(&t0, 0);
  rtl_set_CF(&t0);
  rtl_set_OF(&t0);

  Log("pa2-debug: and result=0x%08x", t2);

  print_asm_template2(and);
}

make_EHelper(xor) {
  rtl_xor(&t2, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t2);

  rtl_update_ZFSF(&t2, id_dest->width);
  rtl_li(&t0, 0);
  rtl_set_CF(&t0);
  rtl_set_OF(&t0);

  Log("pa2-debug: xor result=0x%08x", t2);

  print_asm_template2(xor);
}

make_EHelper(or) {
  rtl_or(&t2, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t2);

  rtl_update_ZFSF(&t2, id_dest->width);
  rtl_li(&t0, 0);
  rtl_set_CF(&t0);
  rtl_set_OF(&t0);

  print_asm_template2(or);
}

make_EHelper(sar) {
  uint32_t count = id_src->val & 0x1f;
  uint32_t result = id_dest->val;
  if (count != 0) {
    uint32_t cf = (id_dest->val >> (count - 1)) & 0x1;
    switch (id_dest->width) {
      case 1: result = (int8_t)id_dest->val >> count; break;
      case 2: result = (int16_t)id_dest->val >> count; break;
      case 4: result = (int32_t)id_dest->val >> count; break;
      default: assert(0);
    }
    rtl_li(&t0, cf);
    rtl_set_CF(&t0);
    rtl_li(&t0, 0);
    rtl_set_OF(&t0);
  }
  rtl_li(&t2, result);
  operand_write(id_dest, &t2);
  if (count != 0) {
    rtl_update_ZFSF(&t2, id_dest->width);
  }

  print_asm_template2(sar);
}

make_EHelper(shl) {
  uint32_t count = id_src->val & 0x1f;
  uint32_t bits = id_dest->width * 8;
  uint32_t mask = bits == 32 ? 0xffffffffu : ((1u << bits) - 1);
  uint32_t value = id_dest->val & mask;
  uint32_t result = value;
  if (count != 0) {
    uint32_t cf = (value >> (bits - count)) & 0x1;
    result = (value << count) & mask;
    rtl_li(&t0, cf);
    rtl_set_CF(&t0);
    if (count == 1) {
      uint32_t of = ((result >> (bits - 1)) & 0x1) ^ cf;
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

  print_asm_template2(shl);
}

make_EHelper(shr) {
  uint32_t count = id_src->val & 0x1f;
  uint32_t bits = id_dest->width * 8;
  uint32_t mask = bits == 32 ? 0xffffffffu : ((1u << bits) - 1);
  uint32_t value = id_dest->val & mask;
  uint32_t result = value;
  if (count != 0) {
    uint32_t cf = (value >> (count - 1)) & 0x1;
    result = value >> count;
    rtl_li(&t0, cf);
    rtl_set_CF(&t0);
    if (count == 1) {
      uint32_t of = (value >> (bits - 1)) & 0x1;
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

make_EHelper(shld) {
  uint32_t count = id_src->val & 0x1f;
  uint32_t bits = id_dest->width * 8;
  uint32_t mask = bits == 32 ? 0xffffffffu : ((1u << bits) - 1);
  uint32_t dest = id_dest->val & mask;
  uint32_t src = id_src2->val & mask;
  uint32_t result = dest;

  if (count != 0) {
    if (count < bits) {
      uint32_t cf = (dest >> (bits - count)) & 0x1;
      result = ((dest << count) | (src >> (bits - count))) & mask;
      rtl_li(&t0, cf);
      rtl_set_CF(&t0);
    }
    else {
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

  print_asm_template3(shld);
}

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
      rtl_li(&t0, cf);
      rtl_set_CF(&t0);
    }
    else {
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

make_EHelper(setcc) {
  uint8_t subcode = decoding.opcode & 0xf;
  rtl_setcc(&t2, subcode);
  operand_write(id_dest, &t2);

  print_asm("set%s %s", get_cc_name(subcode), id_dest->str);
}

make_EHelper(not) {
  t0 = id_dest->val;
  rtl_not(&t0);
  operand_write(id_dest, &t0);

  print_asm_template1(not);
}
