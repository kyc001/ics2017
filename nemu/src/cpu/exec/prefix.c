#include "cpu/exec.h"

make_EHelper(real);
make_EHelper(inv);

make_EHelper(operand_size) {
  decoding.is_operand_size_16 = true;
  exec_real(eip);
  decoding.is_operand_size_16 = false;
}

make_EHelper(rep) {
  if (vaddr_read(*eip, 1) == 0x0f &&
      vaddr_read(*eip + 1, 1) == 0x1e &&
      vaddr_read(*eip + 2, 1) == 0xfb) {
    instr_fetch(eip, 1);
    instr_fetch(eip, 1);
    instr_fetch(eip, 1);
    Log("pa2-debug: treat endbr32 as nop at 0x%08x", cpu.eip);
    print_asm("endbr32");
    return;
  }

  exec_inv(eip);
}
