#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "common.h"
#include "device/mmio.h"
#include "cpu/reg.h"
#include "memory/mmu.h"

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
  assert(len >= 1 && len <= 4);
  uint32_t ret = 0;
  for (int i = 0; i < len; i ++) {
    ret |= (uint32_t)p[i] << (i * 8);
  }
  return ret;
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
  assert(len >= 1 && len <= 4);
  for (int i = 0; i < len; i ++) {
    p[i] = data >> (i * 8);
  }
}

static inline paddr_t page_translate(vaddr_t addr, bool write) {
  paddr_t pdir_base = cpu.cr3 & ~PAGE_MASK;
  paddr_t pde_addr = pdir_base + ((addr >> 22) & 0x3ff) * 4;
  PDE pde;
  pde.val = paddr_read(pde_addr, 4);
  Assert(pde.present, "page directory entry not present: eip = 0x%08x, vaddr = 0x%08x", cpu.eip, addr);
  pde.accessed = 1;
  paddr_write(pde_addr, 4, pde.val);

  paddr_t ptab_base = pde.page_frame << 12;
  paddr_t pte_addr = ptab_base + ((addr >> 12) & 0x3ff) * 4;
  PTE pte;
  pte.val = paddr_read(pte_addr, 4);
  Assert(pte.present, "page table entry not present: eip = 0x%08x, vaddr = 0x%08x", cpu.eip, addr);
  pte.accessed = 1;
  if (write) {
    pte.dirty = 1;
  }
  paddr_write(pte_addr, 4, pte.val);

  return (pte.page_frame << 12) | (addr & PAGE_MASK);
}

static inline bool paging_enabled(void) {
  CR0 cr0;
  cr0.val = cpu.cr0;
  return cr0.paging;
}

static inline uint32_t vaddr_read(vaddr_t addr, int len) {
  if (!paging_enabled()) {
    return paddr_read(addr, len);
  }
  if ((addr & PAGE_MASK) + len > PAGE_SIZE) {
    uint32_t lo_len = PAGE_SIZE - (addr & PAGE_MASK);
    uint32_t lo = vaddr_read(addr, lo_len);
    uint32_t hi = vaddr_read(addr + lo_len, len - lo_len);
    return lo | (hi << (lo_len * 8));
  }
  return paddr_read(page_translate(addr, false), len);
}

static inline void vaddr_write(vaddr_t addr, int len, uint32_t data) {
  if (!paging_enabled()) {
    paddr_write(addr, len, data);
    return;
  }
  if ((addr & PAGE_MASK) + len > PAGE_SIZE) {
    uint32_t lo_len = PAGE_SIZE - (addr & PAGE_MASK);
    uint32_t lo_mask = (1ull << (lo_len * 8)) - 1;
    vaddr_write(addr, lo_len, data & lo_mask);
    vaddr_write(addr + lo_len, len - lo_len, data >> (lo_len * 8));
    return;
  }
  paddr_write(page_translate(addr, true), len, data);
}

#endif
