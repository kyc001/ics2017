#include "common.h"

#ifdef HAS_IOE

#include <sys/time.h>
#include <signal.h>
#include <SDL2/SDL.h>

#define TIMER_HZ 100
#define VGA_HZ 50

static uint64_t jiffy = 0;
static struct itimerval it;
static int device_update_flag = false;
static int update_screen_flag = false;
static uint32_t input_poll_budget = 0;

void init_serial();
void init_timer();
void init_vga();
void init_i8042();

extern void timer_intr();
extern bool timer_try_raise_intr();
extern void send_key(uint8_t, bool);
extern void update_screen();

static void poll_sdl_events(void) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_QUIT:
        exit(0);
      case SDL_KEYDOWN:
      case SDL_KEYUP: {
        if (event.key.repeat == 0) {
          uint8_t k = event.key.keysym.scancode;
          bool is_keydown = (event.key.type == SDL_KEYDOWN);
          send_key(k, is_keydown);
        }
        break;
      }
      default: break;
    }
  }
}

static void timer_sig_handler(int signum) {
  (void)signum;
  jiffy ++;
  timer_intr();

  device_update_flag = true;
  if (jiffy % (TIMER_HZ / VGA_HZ) == 0) {
    update_screen_flag = true;
  }

  int ret = setitimer(ITIMER_REAL, &it, NULL);
  Assert(ret == 0, "Can not set timer");
}

void device_update() {
  if (!device_update_flag) {
    if ((++input_poll_budget & 0x3ff) == 0) {
      poll_sdl_events();
    }
    return;
  }
  input_poll_budget = 0;
  device_update_flag = false;

  (void)timer_try_raise_intr();

  if (update_screen_flag) {
    update_screen();
    update_screen_flag = false;
  }

  poll_sdl_events();
}

bool device_update_in_instr() {
  poll_sdl_events();
  if (timer_try_raise_intr()) {
    return true;
  }
  if (!update_screen_flag) {
    return false;
  }
  update_screen();
  update_screen_flag = false;
  return false;
}

void sdl_clear_event_queue() {
  SDL_Event event;
  while (SDL_PollEvent(&event));
}

void init_device() {
  init_serial();
  init_timer();
  init_vga();
  init_i8042();

  struct sigaction s;
  memset(&s, 0, sizeof(s));
  s.sa_handler = timer_sig_handler;
  int ret = sigaction(SIGALRM, &s, NULL);
  Assert(ret == 0, "Can not set signal handler");

  it.it_value.tv_sec = 0;
  it.it_value.tv_usec = 1000000 / TIMER_HZ;
  ret = setitimer(ITIMER_REAL, &it, NULL);
  Assert(ret == 0, "Can not set timer");
}
#else

void init_device() {
}

#endif	/* HAS_IOE */
