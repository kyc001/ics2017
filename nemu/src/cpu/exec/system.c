#include "cpu/exec.h"
#include "device/port-io.h"

void raise_intr(uint8_t NO, vaddr_t ret_addr);

make_EHelper(lidt) {
  cpu.idtr.limit = vaddr_read(id_dest->addr, 2);
  cpu.idtr.base = vaddr_read(id_dest->addr + 2, 4);

  print_asm_template1(lidt);
}

make_EHelper(mov_r2cr) {
  switch (id_dest->reg) {
    case 0: cpu.cr0 = id_src->val; break;
    case 3: cpu.cr3 = id_src->val; break;
    default: assert(0);
  }

  print_asm("movl %%%s,%%cr%d", reg_name(id_src->reg, 4), id_dest->reg);
}

make_EHelper(mov_cr2r) {
  switch (id_src->reg) {
    case 0: rtl_li(&t0, cpu.cr0); break;
    case 3: rtl_li(&t0, cpu.cr3); break;
    default: assert(0);
  }
  operand_write(id_dest, &t0);

  print_asm("movl %%cr%d,%%%s", id_src->reg, reg_name(id_dest->reg, 4));

#ifdef DIFF_TEST
  extern void diff_test_skip_qemu();
  diff_test_skip_qemu();
#endif
}

make_EHelper(int) {
  raise_intr(id_dest->imm, decoding.seq_eip);

  print_asm("int %s", id_dest->str);

#ifdef DIFF_TEST
  extern void diff_test_skip_nemu();
  diff_test_skip_nemu();
#endif
}

make_EHelper(iret) {
  rtl_pop(&t0);
  decoding.jmp_eip = t0;
  decoding.is_jmp = 1;
  rtl_pop(&t1);
  cpu.cs = t1;
  rtl_pop(&t2);
  cpu.eflags.val = t2;

  print_asm("iret");
}

make_EHelper(in) {
  uint32_t data = 0;
  switch (id_dest->width) {
    case 1: data = pio_read_b(id_src->val); break;
    case 2: data = pio_read_w(id_src->val); break;
    case 4: data = pio_read_l(id_src->val); break;
    default: assert(0);
  }
  rtl_li(&t0, data);
  operand_write(id_dest, &t0);

  print_asm_template2(in);

#ifdef DIFF_TEST
  extern void diff_test_skip_qemu();
  diff_test_skip_qemu();
#endif
}

make_EHelper(out) {
  switch (id_src->width) {
    case 1: pio_write_b(id_dest->val, id_src->val); break;
    case 2: pio_write_w(id_dest->val, id_src->val); break;
    case 4: pio_write_l(id_dest->val, id_src->val); break;
    default: assert(0);
  }

  print_asm_template2(out);

#ifdef DIFF_TEST
  extern void diff_test_skip_qemu();
  diff_test_skip_qemu();
#endif
}

make_EHelper(cli) {
  rtl_li(&t0, 0);
  rtl_set_IF(&t0);
  print_asm("cli");
}

make_EHelper(sti) {
  rtl_li(&t0, 1);
  rtl_set_IF(&t0);
  print_asm("sti");
}

make_EHelper(pushf) {
  rtl_li(&t0, cpu.eflags.val);
  rtl_push(&t0);
  print_asm("pushf");
}

make_EHelper(popf) {
  rtl_pop(&t0);
  cpu.eflags.val = t0;
  print_asm("popf");
}
