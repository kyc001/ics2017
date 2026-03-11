#include "cpu/exec.h"

#ifdef HAS_IOE
extern bool device_update_in_instr(void);
#endif

#define REP_DEVICE_POLL_MASK 0x7f

make_EHelper(mov) {
  operand_write(id_dest, &id_src->val);
  print_asm_template2(mov);
}

make_EHelper(movs) {
  uint32_t count = decoding.rep_prefix ? cpu.ecx : 1;
#ifdef HAS_IOE
  uint32_t budget = 0;
#endif
  while (count > 0) {
    rtl_li(&t0, vaddr_read(cpu.esi, decoding.dest.width));
    vaddr_write(cpu.edi, decoding.dest.width, t0);
    cpu.esi += decoding.dest.width;
    cpu.edi += decoding.dest.width;
    count --;
#ifdef HAS_IOE
    if (decoding.rep_prefix && (++budget & REP_DEVICE_POLL_MASK) == 0) {
      if (device_update_in_instr()) {
        cpu.ecx = count;
        return;
      }
    }
#endif
  }
  if (decoding.rep_prefix) {
    cpu.ecx = 0;
  }

  if (decoding.dest.width == 1) {
    print_asm(decoding.rep_prefix ? "rep movsb" : "movsb");
  }
  else {
    print_asm(decoding.rep_prefix ? "rep movsd" : "movsd");
  }
}

make_EHelper(push) {
  rtl_push(&id_dest->val);

  print_asm_template1(push);
}

make_EHelper(pop) {
  rtl_pop(&t0);
  operand_write(id_dest, &t0);

  print_asm_template1(pop);
}

make_EHelper(pusha) {
  rtl_mv(&t0, &cpu.esp);
  rtl_push(&cpu.eax);
  rtl_push(&cpu.ecx);
  rtl_push(&cpu.edx);
  rtl_push(&cpu.ebx);
  rtl_push(&t0);
  rtl_push(&cpu.ebp);
  rtl_push(&cpu.esi);
  rtl_push(&cpu.edi);

  print_asm("pusha");
}

make_EHelper(popa) {
  rtl_pop(&cpu.edi);
  rtl_pop(&cpu.esi);
  rtl_pop(&cpu.ebp);
  rtl_pop(&t0);
  rtl_pop(&cpu.ebx);
  rtl_pop(&cpu.edx);
  rtl_pop(&cpu.ecx);
  rtl_pop(&cpu.eax);

  print_asm("popa");
}

make_EHelper(leave) {
  rtl_mv(&cpu.esp, &cpu.ebp);
  rtl_pop(&cpu.ebp);

  print_asm("leave");
}

make_EHelper(cltd) {
  if (decoding.is_operand_size_16) {
    rtl_lr(&t0, R_AX, 2);
    rtl_msb(&t1, &t0, 2);
    rtl_li(&t0, t1 ? 0xffff : 0);
    rtl_sr(R_DX, 2, &t0);
  }
  else {
    rtl_msb(&t0, &cpu.eax, 4);
    rtl_li(&cpu.edx, t0 ? 0xffffffffu : 0);
  }

  print_asm(decoding.is_operand_size_16 ? "cwtl" : "cltd");
}

make_EHelper(cwtl) {
  if (decoding.is_operand_size_16) {
    rtl_lr(&t0, R_AL, 1);
    rtl_sext(&t1, &t0, 1);
    rtl_sr(R_AX, 2, &t1);
  }
  else {
    rtl_lr(&t0, R_AX, 2);
    rtl_sext(&cpu.eax, &t0, 2);
  }

  print_asm(decoding.is_operand_size_16 ? "cbtw" : "cwtl");
}

make_EHelper(movsx) {
  id_dest->width = decoding.is_operand_size_16 ? 2 : 4;
  rtl_sext(&t2, &id_src->val, id_src->width);
  operand_write(id_dest, &t2);
  print_asm_template2(movsx);
}

make_EHelper(movzx) {
  id_dest->width = decoding.is_operand_size_16 ? 2 : 4;
  operand_write(id_dest, &id_src->val);
  print_asm_template2(movzx);
}

make_EHelper(lea) {
  rtl_li(&t2, id_src->addr);
  operand_write(id_dest, &t2);
  print_asm_template2(lea);
}

make_EHelper(stos) {
  uint32_t count = decoding.rep_prefix ? cpu.ecx : 1;
#ifdef HAS_IOE
  uint32_t budget = 0;
#endif
  while (count > 0) {
    vaddr_write(cpu.edi, decoding.dest.width, cpu.eax);
    cpu.edi += decoding.dest.width;
    count --;
#ifdef HAS_IOE
    if (decoding.rep_prefix && (++budget & REP_DEVICE_POLL_MASK) == 0) {
      if (device_update_in_instr()) {
        cpu.ecx = count;
        return;
      }
    }
#endif
  }
  if (decoding.rep_prefix) {
    cpu.ecx = 0;
  }
  print_asm(decoding.dest.width == 1 ?
      (decoding.rep_prefix ? "rep stosb" : "stosb") :
      (decoding.rep_prefix ? "rep stosd" : "stos"));
}
