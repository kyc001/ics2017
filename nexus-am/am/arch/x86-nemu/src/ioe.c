#include <am.h>
#include <x86.h>

#define RTC_PORT 0x48   // Note that this is not standard
#define KBD_PORT 0x60
#define SCREEN_PORT 0x100 // Note that this is not standard
static unsigned long boot_time;

void _ioe_init() {
  uint32_t size = inl(SCREEN_PORT);
  _screen.width = size >> 16;
  _screen.height = size & 0xffff;
  boot_time = inl(RTC_PORT);
}

unsigned long _uptime() {
  return inl(RTC_PORT) - boot_time;
}

uint32_t* const fb = (uint32_t *)0x40000;

_Screen _screen = {
  .width  = 0,
  .height = 0,
};

extern void* memcpy(void *, const void *, int);

void _draw_rect(const uint32_t *pixels, int x, int y, int w, int h) {
  for (int row = 0; row < h; row ++) {
    memcpy(fb + (y + row) * _screen.width + x, pixels + row * w,
        w * sizeof(uint32_t));
  }
}

void _draw_sync() {
}

int _read_key() {
  return inl(KBD_PORT);
}
