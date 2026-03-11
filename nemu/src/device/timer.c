#include "device/port-io.h"
#include "monitor/monitor.h"
#include <sys/time.h>

#define RTC_PORT 0x48   // Note that this is not the standard

static volatile int timer_intr_pending = 0;
static struct timeval boot_time;

void timer_intr() {
  if (nemu_state == NEMU_RUNNING) {
    timer_intr_pending = 1;
  }
}

bool timer_try_raise_intr() {
  if (timer_intr_pending) {
    timer_intr_pending = 0;
    extern void dev_raise_intr(void);
    dev_raise_intr();
    return true;
  }
  return false;
}

static uint32_t *rtc_port_base;

static uint32_t get_elapsed_ms(void) {
  struct timeval now;
  gettimeofday(&now, NULL);

  int64_t sec = (int64_t)now.tv_sec - (int64_t)boot_time.tv_sec;
  int64_t usec = (int64_t)now.tv_usec - (int64_t)boot_time.tv_usec;
  if (usec < 0) {
    sec --;
    usec += 1000000;
  }

  return (uint32_t)((uint64_t)sec * 1000 + (uint64_t)(usec + 500) / 1000);
}

void rtc_io_handler(ioaddr_t addr, int len, bool is_write) {
  (void)addr;
  (void)len;
  if (!is_write) {
    rtc_port_base[0] = get_elapsed_ms();
  }
}

void init_timer() {
  gettimeofday(&boot_time, NULL);
  rtc_port_base = add_pio_map(RTC_PORT, 4, rtc_io_handler);
}
