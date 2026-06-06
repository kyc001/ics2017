#include "common.h"

/* Uncomment these macros to enable corresponding functionality. */
#define HAS_ASYE
#define HAS_PTE

#ifndef DEFAULT_PROGRAM
#define DEFAULT_PROGRAM "/bin/pal"
#endif

#ifndef SECOND_PROGRAM
#define SECOND_PROGRAM "/bin/hello"
#endif

#ifndef THIRD_PROGRAM
#define THIRD_PROGRAM "/bin/videotest"
#endif

void init_mm(void);
void init_ramdisk(void);
void init_device(void);
void init_irq(void);
void init_fs(void);
uint32_t loader(_Protect *, const char *);
void load_prog(const char *filename);

int main() {
#ifdef HAS_PTE
  init_mm();
#endif

  Log("'Hello World!' from Nanos-lite");
  Log("Build time: %s, %s", __TIME__, __DATE__);

  init_ramdisk();

  init_device();

#ifdef HAS_ASYE
  Log("Initializing interrupt/exception handler...");
  init_irq();
#endif

  init_fs();

#ifdef HAS_ASYE
  load_prog(DEFAULT_PROGRAM);
  load_prog(SECOND_PROGRAM);
  load_prog(THIRD_PROGRAM);
  _trap();
#else
  uint32_t entry = loader(NULL, DEFAULT_PROGRAM);
  ((void (*)(void))entry)();
#endif

  panic("Should not reach here");
}
