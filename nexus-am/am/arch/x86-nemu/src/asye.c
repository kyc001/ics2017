#include <am.h>
#include <x86.h>

static _RegSet* (*H)(_Event, _RegSet*) = NULL;

void vecsys();
void veckern();
void vectime();
void vecnull();

_RegSet* irq_handle(_RegSet *tf) {
  _RegSet *next = tf;
  if (H) {
    _Event ev;
    switch (tf->irq) {
      case 0x80: ev.event = _EVENT_SYSCALL; break;
      case 0x81: ev.event = _EVENT_TRAP; break;
      case 32: ev.event = _EVENT_IRQ_TIME; break;
      default: ev.event = _EVENT_ERROR; break;
    }

    next = H(ev, tf);
    if (next == NULL) {
      next = tf;
    }
  }

  return next;
}

static GateDesc idt[NR_IRQ];

void _asye_init(_RegSet*(*h)(_Event, _RegSet*)) {
  // initialize IDT
  for (unsigned int i = 0; i < NR_IRQ; i ++) {
    idt[i] = GATE(STS_TG32, KSEL(SEG_KCODE), vecnull, DPL_KERN);
  }

  // -------------------- system call --------------------------
  idt[0x80] = GATE(STS_TG32, KSEL(SEG_KCODE), vecsys, DPL_USER);
  idt[0x81] = GATE(STS_TG32, KSEL(SEG_KCODE), veckern, DPL_KERN);
  idt[32] = GATE(STS_IG32, KSEL(SEG_KCODE), vectime, DPL_KERN);

  set_idt(idt, sizeof(idt));

  // register event handler
  H = h;
}

_RegSet *_make(_Area stack, void *entry, void *arg) {
  return NULL;
}

void _trap() {
  asm volatile("int $0x81");
}

int _istatus(int enable) {
  uint32_t eflags;
  asm volatile("pushfl; popl %0" : "=r"(eflags));
  if (enable) {
    asm volatile("sti");
  }
  else {
    asm volatile("cli");
  }
  return (eflags & FL_IF) != 0;
}
