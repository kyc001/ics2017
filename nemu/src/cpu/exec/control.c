#include "cpu/exec.h"

make_EHelper(jmp) {
  // the target address is calculated at the decode stage
  decoding.is_jmp = 1;

  print_asm("jmp %x", decoding.jmp_eip);
}

make_EHelper(jcc) {
  // the target address is calculated at the decode stage
  uint8_t subcode = decoding.opcode & 0xf;
  rtl_setcc(&t2, subcode);
  decoding.is_jmp = t2;

  print_asm("j%s %x", get_cc_name(subcode), decoding.jmp_eip);
}

make_EHelper(jmp_rm) {
  decoding.jmp_eip = id_dest->val;
  decoding.is_jmp = 1;

  print_asm("jmp *%s", id_dest->str);
}

make_EHelper(call) {
  // the target address is calculated at the decode stage
  rtl_li(&t0, decoding.seq_eip);
  Log("pa2-debug: call return=0x%08x target=0x%08x", decoding.seq_eip, decoding.jmp_eip);
  rtl_push(&t0);
  decoding.is_jmp = 1;

  print_asm("call %x", decoding.jmp_eip);
}

make_EHelper(ret) {
  rtl_pop(&t0);
  Log("pa2-debug: ret target=0x%08x", t0);
  decoding.jmp_eip = t0;
  decoding.is_jmp = 1;

  print_asm("ret");
}

make_EHelper(call_rm) {
  rtl_li(&t0, decoding.seq_eip);
  rtl_push(&t0);
  decoding.jmp_eip = id_dest->val;
  decoding.is_jmp = 1;

  print_asm("call *%s", id_dest->str);
}
