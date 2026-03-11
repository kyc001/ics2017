#include "cpu/exec.h"
#include "memory/mmu.h"

void raise_intr(uint8_t NO, vaddr_t ret_addr) {
  /* TODO: Trigger an interrupt/exception with ``NO''.
   * That is, use ``NO'' to index the IDT.
   */
  rtl_li(&t0, cpu.eflags.val);
  rtl_push(&t0);
  rtl_li(&t0, cpu.cs ? cpu.cs : 0x8);
  rtl_push(&t0);
  rtl_li(&t0, ret_addr);
  rtl_push(&t0);
  cpu.eflags.IF = 0;

  GateDesc desc;
  desc.val = vaddr_read(cpu.idtr.base + NO * 8, 4);
  *(((uint32_t *)&desc) + 1) = vaddr_read(cpu.idtr.base + NO * 8 + 4, 4);
  decoding.jmp_eip = (desc.offset_31_16 << 16) | desc.offset_15_0;
  decoding.is_jmp = 1;
}

void dev_raise_intr() {
  if (cpu.eflags.IF) {
    raise_intr(32, cpu.eip);
  }
}
