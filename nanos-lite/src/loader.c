#include "common.h"
#include "fs.h"

#define DEFAULT_ENTRY ((void *)0x4000000)

size_t get_ramdisk_size();
size_t ramdisk_read(void *buf, off_t offset, size_t len);

uintptr_t loader(_Protect *as, const char *filename) {
  (void)as;

  if (filename != NULL) {
    int fd = fs_open(filename, 0, 0);
    if (fd >= 0) {
      size_t size = fs_filesz(fd);
      size_t nread = fs_read(fd, DEFAULT_ENTRY, size);
      assert(nread == size);
      fs_close(fd);
      return (uintptr_t)DEFAULT_ENTRY;
    }
  }

  size_t size = get_ramdisk_size();
  ramdisk_read(DEFAULT_ENTRY, 0, size);
  return (uintptr_t)DEFAULT_ENTRY;
}
