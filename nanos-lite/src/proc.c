#include "proc.h"

#define MAX_NR_PROC 4

static PCB pcb[MAX_NR_PROC];
static int nr_proc = 0;
PCB *current = NULL;
static PCB *current_game = NULL;

uintptr_t loader(_Protect *as, const char *filename);
uintptr_t loader_brk(void);

static void init_user_process(PCB *p, const char *filename) {
  _protect(&p->as);

  uintptr_t entry = loader(&p->as, filename);
  p->cur_brk = loader_brk();
  p->max_brk = p->cur_brk;

  _Area stack;
  stack.start = p->stack;
  stack.end = stack.start + sizeof(p->stack);

  p->tf = _umake(&p->as, stack, stack, (void *)entry, NULL, NULL);
}

void load_prog(const char *filename) {
  int i = nr_proc ++;
  assert(i < MAX_NR_PROC);
  init_user_process(&pcb[i], filename);
  if (i == 0) {
    current_game = &pcb[i];
  }
}

_RegSet *exec_prog(const char *filename) {
  assert(current != NULL);
  init_user_process(current, filename);
  _switch(&current->as);
  return current->tf;
}

void switch_game(void) {
  if (nr_proc >= 3) {
    current_game = current_game == &pcb[0] ? &pcb[2] : &pcb[0];
  }
}

_RegSet* schedule(_RegSet *prev) {
  if (current != NULL) {
    current->tf = prev;
  }
  if (nr_proc == 0) {
    return NULL;
  }

  PCB *next = &pcb[0];
  if (nr_proc >= 2) {
    static int tick = 0;
    if (current == &pcb[1]) {
      next = current_game ? current_game : &pcb[0];
    }
    else if ((++ tick % 8) == 0) {
      next = &pcb[1];
    }
    else {
      next = current_game ? current_game : &pcb[0];
    }
  }

  current = next;
  _switch(&current->as);
  return current->tf;
}
