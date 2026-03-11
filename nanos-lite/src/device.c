#include "common.h"

typedef struct {
  uint32_t *pixels;
  int x, y, w, h;
} NDL_FbBlitReq;

size_t serial_write(const void *buf, size_t offset, size_t len) {
  (void)offset;
  const char *p = buf;
  for (size_t i = 0; i < len; i ++) {
    _putc(p[i]);
  }
  return len;
}

size_t serial_read(void *buf, size_t offset, size_t len) {
  (void)buf;
  (void)offset;
  (void)len;
  return 0;
}

#define NAME(key) \
  [_KEY_##key] = #key,

static const char *keyname[256] __attribute__((used)) = {
  [_KEY_NONE] = "NONE",
  _KEYS(NAME)
};

size_t events_read(void *buf, size_t offset, size_t len) {
  (void)offset;
  int key = _read_key();
  if (key == _KEY_NONE) {
    int n = snprintf(buf, len, "t %u\n", (unsigned)_uptime());
    return n > 0 ? n : 0;
  }

  bool down = false;
  if (key & 0x8000) {
    key ^= 0x8000;
    down = true;
  }

  int n = snprintf(buf, len, "k%c %s\n", down ? 'd' : 'u', keyname[key]);
  return n > 0 ? n : 0;
}

static char dispinfo[128] __attribute__((used));

size_t dispinfo_read(void *buf, off_t offset, size_t len) {
  size_t n = strlen(dispinfo);
  if ((size_t)offset >= n) {
    return 0;
  }
  if ((size_t)offset + len > n) {
    len = n - offset;
  }
  memcpy(buf, dispinfo + offset, len);
  return len;
}

size_t fb_write(const void *buf, off_t offset, size_t len) {
  if (len == sizeof(NDL_FbBlitReq)) {
    const NDL_FbBlitReq *req = (const NDL_FbBlitReq *)buf;
    _draw_rect(req->pixels, req->x, req->y, req->w, req->h);
    return len;
  }

  int width = _screen.width;
  int pixel_offset = offset / (int)sizeof(uint32_t);
  int pixels = len / (int)sizeof(uint32_t);
  const uint32_t *p = (const uint32_t *)buf;
  int x = pixel_offset % width;
  int y = pixel_offset / width;

  if (x == 0 && pixels % width == 0) {
    _draw_rect(p, 0, y, width, pixels / width);
    return len;
  }

  while (pixels > 0) {
    int cur_x = pixel_offset % width;
    int cur_y = pixel_offset / width;
    int row_pixels = width - cur_x;
    if (row_pixels > pixels) {
      row_pixels = pixels;
    }
    _draw_rect(p, cur_x, cur_y, row_pixels, 1);
    p += row_pixels;
    pixel_offset += row_pixels;
    pixels -= row_pixels;
  }
  return len;
}

void init_device() {
  _ioe_init();

  snprintf(dispinfo, sizeof(dispinfo), "WIDTH: %d\nHEIGHT: %d\n",
      _screen.width, _screen.height);
}
