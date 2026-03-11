#include "common.h"

#ifdef HAS_IOE

#include <ctype.h>
#include <fcntl.h>
#include <SDL2/SDL.h>
#include <signal.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#define TIMER_HZ 100
#define VGA_HZ 50

static uint64_t jiffy = 0;
static struct itimerval it;
static int device_update_flag = false;
static int update_screen_flag = false;
static uint32_t input_poll_budget = 0;
static bool terminal_input_active = false;
static struct termios terminal_saved_termios;
static int terminal_saved_flags = -1;
static int terminal_esc_state = 0;

void init_serial();
void init_timer();
void init_vga();
void init_i8042();

extern void timer_intr();
extern bool timer_try_raise_intr();
extern void send_key(uint8_t, bool);
extern void update_screen();

static void restore_terminal_input(void) {
  if (!terminal_input_active) {
    return;
  }
  tcsetattr(STDIN_FILENO, TCSANOW, &terminal_saved_termios);
  if (terminal_saved_flags >= 0) {
    fcntl(STDIN_FILENO, F_SETFL, terminal_saved_flags);
  }
  terminal_input_active = false;
}

static void init_terminal_input(void) {
  if (getenv("NEMU_TERMINAL_KEYS_ACTIVE") == NULL || !isatty(STDIN_FILENO)) {
    return;
  }

  int ret = tcgetattr(STDIN_FILENO, &terminal_saved_termios);
  if (ret != 0) {
    return;
  }

  struct termios raw = terminal_saved_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
  raw.c_cflag |= CS8;
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  ret = tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  if (ret != 0) {
    return;
  }

  terminal_saved_flags = fcntl(STDIN_FILENO, F_GETFL);
  if (terminal_saved_flags >= 0) {
    fcntl(STDIN_FILENO, F_SETFL, terminal_saved_flags | O_NONBLOCK);
  }

  terminal_input_active = true;
  atexit(restore_terminal_input);
  fprintf(stderr, "note: headless terminal keyboard mode active. Type keys in this terminal; Ctrl-C to quit.\n");
}

static int ascii_to_scancode(int ch) {
  if (isalpha(ch)) {
    return SDL_SCANCODE_A + tolower(ch) - 'a';
  }

  if (ch >= '1' && ch <= '9') {
    return SDL_SCANCODE_1 + ch - '1';
  }

  switch (ch) {
    case '0': return SDL_SCANCODE_0;
    case ' ': return SDL_SCANCODE_SPACE;
    case '\n':
    case '\r': return SDL_SCANCODE_RETURN;
    case '\t': return SDL_SCANCODE_TAB;
    case 0x7f:
    case '\b': return SDL_SCANCODE_BACKSPACE;
    case '-': return SDL_SCANCODE_MINUS;
    case '=': return SDL_SCANCODE_EQUALS;
    case '[': return SDL_SCANCODE_LEFTBRACKET;
    case ']': return SDL_SCANCODE_RIGHTBRACKET;
    case '\\': return SDL_SCANCODE_BACKSLASH;
    case ';': return SDL_SCANCODE_SEMICOLON;
    case '\'': return SDL_SCANCODE_APOSTROPHE;
    case ',': return SDL_SCANCODE_COMMA;
    case '.': return SDL_SCANCODE_PERIOD;
    case '/': return SDL_SCANCODE_SLASH;
    case '`': return SDL_SCANCODE_GRAVE;
    default: return SDL_SCANCODE_UNKNOWN;
  }
}

static void inject_terminal_key(uint8_t scancode) {
  if (scancode == SDL_SCANCODE_UNKNOWN) {
    return;
  }
  send_key(scancode, true);
  send_key(scancode, false);
}

static void poll_terminal_input(void) {
  if (!terminal_input_active) {
    return;
  }

  char buf[64];
  ssize_t nread = read(STDIN_FILENO, buf, sizeof(buf));
  if (nread <= 0) {
    return;
  }

  for (ssize_t i = 0; i < nread; i ++) {
    unsigned char ch = (unsigned char)buf[i];

    if (terminal_esc_state == 1) {
      if (ch == '[') {
        terminal_esc_state = 2;
        continue;
      }
      terminal_esc_state = 0;
      inject_terminal_key(SDL_SCANCODE_ESCAPE);
    }
    else if (terminal_esc_state == 2) {
      switch (ch) {
        case 'A': inject_terminal_key(SDL_SCANCODE_UP); break;
        case 'B': inject_terminal_key(SDL_SCANCODE_DOWN); break;
        case 'C': inject_terminal_key(SDL_SCANCODE_RIGHT); break;
        case 'D': inject_terminal_key(SDL_SCANCODE_LEFT); break;
        case 'H': inject_terminal_key(SDL_SCANCODE_HOME); break;
        case 'F': inject_terminal_key(SDL_SCANCODE_END); break;
        default: inject_terminal_key(SDL_SCANCODE_ESCAPE); break;
      }
      terminal_esc_state = 0;
      continue;
    }

    if (ch == 0x1b) {
      terminal_esc_state = 1;
      continue;
    }

    inject_terminal_key(ascii_to_scancode(ch));
  }
}

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
    poll_terminal_input();
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

  poll_terminal_input();
  poll_sdl_events();
}

bool device_update_in_instr() {
  poll_terminal_input();
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
  init_terminal_input();

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
