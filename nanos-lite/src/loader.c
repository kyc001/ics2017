#include "common.h"
#include "fs.h"
#include "memory.h"

#define DEFAULT_ENTRY ((void *)0x8048000)

static uintptr_t last_brk = (uintptr_t)DEFAULT_ENTRY;

uintptr_t loader_brk(void) {
  return last_brk;
}

uintptr_t loader(_Protect *as, const char *filename) {
  if (filename == NULL) {
    filename = "/bin/dummy";
  }

  int fd = fs_open(filename, 0, 0);
  assert(fd >= 0);

  size_t size = fs_lseek(fd, 0, SEEK_END);
  fs_lseek(fd, 0, SEEK_SET);
  uintptr_t start = (uintptr_t)DEFAULT_ENTRY;
  uintptr_t end = start + size;
  last_brk = PGROUNDUP(end);

  if (as == NULL) {
    size_t ret = fs_read(fd, DEFAULT_ENTRY, size);
    assert(ret == size);
  }
  else {
    uintptr_t va = PGROUNDDOWN(start);
    while (va < end) {
      void *pa = new_page();
      memset(pa, 0, PGSIZE);
      _map(as, (void *)va, pa);

      size_t page_offset = start > va ? start - va : 0;
      size_t n = PGSIZE - page_offset;
      if (va + page_offset + n > end) {
        n = end - va - page_offset;
      }
      size_t ret = fs_read(fd, (void *)((uintptr_t)pa + page_offset), n);
      assert(ret == n);
      va += PGSIZE;
    }
  }

  fs_close(fd);
  return (uintptr_t)DEFAULT_ENTRY;
}
