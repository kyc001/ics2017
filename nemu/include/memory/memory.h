#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "common.h"
#include "device/mmio.h"

#define PMEM_SIZE (128 * 1024 * 1024)

extern uint8_t pmem[];

/* convert the guest physical address in the guest program to host virtual address in NEMU */
#define guest_to_host(p) ((void *)(pmem + (unsigned)p))
/* convert the host virtual address in NEMU to guest physical address in the guest program */
#define host_to_guest(p) ((paddr_t)((void *)p - (void *)pmem))

static inline uint32_t paddr_read(paddr_t addr, int len) {
  int map_NO = is_mmio(addr);
  if (map_NO != -1) {
    return mmio_read(addr, len, map_NO);
  }

  Assert(addr + len - 1 < PMEM_SIZE,
      "physical address(0x%08x) is out of bound", addr);
  uint8_t *p = (uint8_t *)guest_to_host(addr);
  switch (len) {
    case 1: return p[0];
    case 2: return *(uint16_t *)p;
    case 4: return *(uint32_t *)p;
    default: assert(0); return 0;
  }
}

static inline void paddr_write(paddr_t addr, int len, uint32_t data) {
  int map_NO = is_mmio(addr);
  if (map_NO != -1) {
    mmio_write(addr, len, data, map_NO);
    return;
  }

  Assert(addr + len - 1 < PMEM_SIZE,
      "physical address(0x%08x) is out of bound", addr);
  uint8_t *p = (uint8_t *)guest_to_host(addr);
  switch (len) {
    case 1: *(uint8_t *)p = (uint8_t)data; return;
    case 2: *(uint16_t *)p = (uint16_t)data; return;
    case 4: *(uint32_t *)p = data; return;
    default: assert(0); return;
  }
}

static inline uint32_t vaddr_read(vaddr_t addr, int len) {
  return paddr_read(addr, len);
}

static inline void vaddr_write(vaddr_t addr, int len, uint32_t data) {
  paddr_write(addr, len, data);
}

#endif
