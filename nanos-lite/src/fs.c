#include "fs.h"

typedef size_t (*ReadFn)(void *buf, size_t offset, size_t len);
typedef size_t (*WriteFn)(const void *buf, size_t offset, size_t len);

typedef struct {
  char *name;
  size_t size;
  size_t disk_offset;
  ReadFn read;
  WriteFn write;
} Finfo;

typedef struct {
  bool used;
  int file_idx;
  size_t open_offset;
} FdInfo;

enum {FD_STDIN, FD_STDOUT, FD_STDERR, FD_FB, FD_EVENTS, FD_DISPINFO};

size_t serial_write(const void *buf, size_t offset, size_t len);
size_t serial_read(void *buf, size_t offset, size_t len);
size_t events_read(void *buf, size_t offset, size_t len);
size_t dispinfo_read(void *buf, size_t offset, size_t len);
size_t fb_write(const void *buf, size_t offset, size_t len);
size_t ramdisk_read(void *buf, size_t offset, size_t len);
size_t ramdisk_write(const void *buf, size_t offset, size_t len);

static size_t invalid_read(void *buf, size_t offset, size_t len) {
  panic("should not reach here");
  return 0;
}

static size_t invalid_write(const void *buf, size_t offset, size_t len) {
  panic("should not reach here");
  return 0;
}

/* This is the information about all files in disk. */
static Finfo file_table[] __attribute__((used)) = {
  {"stdin", 0, 0, invalid_read, invalid_write},
  {"stdout", 0, 0, invalid_read, serial_write},
  {"stderr", 0, 0, invalid_read, serial_write},
  {"/dev/fb", 0, 0, invalid_read, fb_write},
  {"/dev/events", 0, 0, events_read, invalid_write},
  {"/proc/dispinfo", 128, 0, dispinfo_read, invalid_write},
#include "files.h"
};

#define NR_FILES (sizeof(file_table) / sizeof(file_table[0]))
#define NR_OPEN_FILES 64
static FdInfo open_table[NR_OPEN_FILES];

void init_fs() {
  file_table[FD_FB].size = _screen.width * _screen.height * sizeof(uint32_t);
  memset(open_table, 0, sizeof(open_table));
  open_table[0] = (FdInfo){ .used = true, .file_idx = FD_STDIN, .open_offset = 0 };
  open_table[1] = (FdInfo){ .used = true, .file_idx = FD_STDOUT, .open_offset = 0 };
  open_table[2] = (FdInfo){ .used = true, .file_idx = FD_STDERR, .open_offset = 0 };
}

int fs_open(const char *pathname, int flags, int mode) {
  (void)flags;
  (void)mode;
  int file_idx = -1;
  for (int i = 0; i < NR_FILES; i ++) {
    if (strcmp(pathname, file_table[i].name) == 0) {
      file_idx = i;
      break;
    }
  }
  if (file_idx < 0) {
    return -1;
  }

  for (int fd = 3; fd < NR_OPEN_FILES; fd ++) {
    if (!open_table[fd].used) {
      open_table[fd].used = true;
      open_table[fd].file_idx = file_idx;
      open_table[fd].open_offset = 0;
      return fd;
    }
  }
  return -1;
}

size_t fs_read(int fd, void *buf, size_t len) {
  assert(fd >= 0 && fd < NR_OPEN_FILES && open_table[fd].used);
  Finfo *f = &file_table[open_table[fd].file_idx];
  ReadFn reader = f->read ? f->read : ramdisk_read;
  if (reader == invalid_read) {
    return 0;
  }
  size_t rest = f->size > open_table[fd].open_offset ? f->size - open_table[fd].open_offset : 0;
  if (f->size != 0 && len > rest) {
    len = rest;
  }
  size_t ret = reader(buf, open_table[fd].open_offset + f->disk_offset, len);
  open_table[fd].open_offset += ret;
  return ret;
}

size_t fs_write(int fd, const void *buf, size_t len) {
  assert(fd >= 0 && fd < NR_OPEN_FILES && open_table[fd].used);
  Finfo *f = &file_table[open_table[fd].file_idx];
  WriteFn writer = f->write ? f->write : ramdisk_write;
  if (writer == invalid_write) {
    return 0;
  }
  size_t ret = writer(buf, open_table[fd].open_offset + f->disk_offset, len);
  open_table[fd].open_offset += ret;
  return ret;
}

size_t fs_lseek(int fd, size_t offset, int whence) {
  assert(fd >= 0 && fd < NR_OPEN_FILES && open_table[fd].used);
  Finfo *f = &file_table[open_table[fd].file_idx];
  switch (whence) {
    case SEEK_SET: open_table[fd].open_offset = offset; break;
    case SEEK_CUR: open_table[fd].open_offset += offset; break;
    case SEEK_END: open_table[fd].open_offset = f->size + offset; break;
    default: panic("invalid whence = %d", whence);
  }
  return open_table[fd].open_offset;
}

int fs_close(int fd) {
  assert(fd >= 0 && fd < NR_OPEN_FILES && open_table[fd].used);
  if (fd >= 3) {
    open_table[fd].used = false;
    open_table[fd].file_idx = 0;
    open_table[fd].open_offset = 0;
  }
  return 0;
}
