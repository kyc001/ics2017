#include "common.h"
#include "syscall.h"
#include "fs.h"
#include "proc.h"

int mm_brk(uint32_t new_brk);
_RegSet *exec_prog(const char *filename);

_RegSet* do_syscall(_RegSet *r) {
  uintptr_t a[4];
  a[0] = SYSCALL_ARG1(r);
  a[1] = SYSCALL_ARG2(r);
  a[2] = SYSCALL_ARG3(r);
  a[3] = SYSCALL_ARG4(r);

  switch (a[0]) {
    case SYS_none:
      r->eax = 1;
      break;
    case SYS_exit:
      _halt(a[1]);
      break;
    case SYS_open:
      r->eax = fs_open((const char *)a[1], a[2], a[3]);
      break;
    case SYS_read:
      r->eax = fs_read(a[1], (void *)a[2], a[3]);
      break;
    case SYS_write:
      r->eax = fs_write(a[1], (const void *)a[2], a[3]);
      break;
    case SYS_close:
      r->eax = fs_close(a[1]);
      break;
    case SYS_lseek:
      r->eax = fs_lseek(a[1], (ssize_t)a[2], a[3]);
      break;
    case SYS_brk:
      r->eax = mm_brk(a[1]);
      break;
    case SYS_execve:
      return exec_prog((const char *)a[1]);
    case SYS_gettimeofday: {
      struct timeval {
        long tv_sec;
        long tv_usec;
      };
      struct timeval *tv = (struct timeval *)a[1];
      if (tv != NULL) {
        uint32_t ms = _uptime();
        tv->tv_sec = ms / 1000;
        tv->tv_usec = (ms % 1000) * 1000;
      }
      r->eax = 0;
      break;
    }
    default: panic("Unhandled syscall ID = %d", a[0]);
  }

  return r;
}
