// device.c - 设备管理和事件循环（PA3 核心文件）
// 功能：统一管理所有模拟设备（定时器、键盘、VGA 等），处理设备事件
// 这是 NEMU I/O 扩展（IOE）的核心调度器

#include "common.h"       // NEMU 公共定义

#ifdef HAS_IOE            // 只有定义了 HAS_IOE 才编译这些代码
                          // HAS_IOE = Has I/O Extension（有 I/O 扩展）

#include <ctype.h>        // isalpha(), tolower() 等字符处理函数
#include <fcntl.h>        // fcntl() 文件控制
#include <SDL2/SDL.h>     // SDL 库（用于图形界面和键盘事件）
#include <signal.h>       // 信号处理（定时器信号）
#include <sys/time.h>     // setitimer(), itimerval 结构体
#include <termios.h>      // 终端控制（设置非规范模式）
#include <unistd.h>       // read(), STDIN_FILENO

#define TIMER_HZ 100      // 定时器频率：100Hz（每秒 100 次中断）
                          // 每次中断间隔 = 1/100 秒 = 10 毫秒
#define VGA_HZ 50         // 屏幕刷新频率：50Hz（每秒刷新 50 次）
                          // 每次刷新间隔 = 1/50 秒 = 20 毫秒

static uint64_t jiffy = 0;                // 系统滴答计数（每次定时器中断 +1）
                                          // jiffy = 自启动以来的定时器中断次数
static struct itimerval it;               // 定时器配置结构体
static int device_update_flag = false;    // 设备更新标志（定时器中断时置 1）
static int update_screen_flag = false;    // 屏幕更新标志（每 2 次定时器中断置 1）
static uint32_t input_poll_budget = 0;   // 输入轮询预算（避免频繁轮询）
static bool terminal_input_active = false;// 终端键盘模式是否激活
static struct termios terminal_saved_termios;  // 保存的终端设置
static int terminal_saved_flags = -1;     // 保存的终端标志
static int terminal_esc_state = 0;        // ESC 序列解析状态（用于方向键等）

// 外部函数声明
void init_serial();       // 初始化串口（serial.c）
void init_timer();        // 初始化定时器（timer.c）
void init_vga();          // 初始化 VGA（vga.c）
void init_i8042();        // 初始化键盘控制器（keyboard.c）

extern void timer_intr(); // 定时器中断处理（timer.c）
extern bool timer_try_raise_intr();  // 尝试触发定时器中断
extern void send_key(uint8_t, bool); // 发送按键事件（keyboard.c）
extern void update_screen();         // 更新屏幕显示（vga.c）

// restore_terminal_input: 恢复终端原始设置
// 功能：程序退出时恢复终端的规范模式
// 调用时机：atexit 注册的退出处理函数
static void restore_terminal_input(void) {
  if (!terminal_input_active) {         // 如果终端模式未激活，直接返回
    return;
  }
  tcsetattr(STDIN_FILENO, TCSANOW, &terminal_saved_termios);
  // 恢复原始终端设置（规范模式、回显等）
  if (terminal_saved_flags >= 0) {
    fcntl(STDIN_FILENO, F_SETFL, terminal_saved_flags);
    // 恢复原始文件标志（阻塞模式）
  }
  terminal_input_active = false;
}

// init_terminal_input: 初始化终端键盘输入
// 功能：设置终端为非规范模式，可以逐字符读取键盘输入
// 用途：无头模式（headless）下，直接从终端读取按键发送给客户程序
static void init_terminal_input(void) {
  if (getenv("NEMU_TERMINAL_KEYS_ACTIVE") == NULL || !isatty(STDIN_FILENO)) {
    return;                             // 未设置环境变量或 stdin 不是终端，不激活
  }

  int ret = tcgetattr(STDIN_FILENO, &terminal_saved_termios);
  // 获取当前终端设置并保存
  if (ret != 0) {
    return;                             // 获取失败，不激活
  }

  struct termios raw = terminal_saved_termios;
  // 设置非规范模式（raw mode）：
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  // 输入标志：禁用中断、回车转换、奇偶校验、XON/XOFF 流控制
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
  // 本地标志：禁用回显、规范模式（逐行输入）、扩展功能
  raw.c_cflag |= CS8;                   // 8 位字符
  raw.c_cc[VMIN] = 0;                   // 非阻塞读取（最少 0 字符）
  raw.c_cc[VTIME] = 0;                  // 超时 0（立即返回）
  ret = tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  if (ret != 0) {
    return;
  }

  terminal_saved_flags = fcntl(STDIN_FILENO, F_GETFL);
  if (terminal_saved_flags >= 0) {
    fcntl(STDIN_FILENO, F_SETFL, terminal_saved_flags | O_NONBLOCK);
    // 设置非阻塞 I/O（read 立即返回）
  }

  terminal_input_active = true;
  atexit(restore_terminal_input);       // 注册退出处理函数
  fprintf(stderr, "note: headless terminal keyboard mode active. Type keys in this terminal; Ctrl-C to quit.\n");
}

// ascii_to_scancode: ASCII 字符 → 键盘扫描码
// 功能：把终端输入的 ASCII 字符转换成 SDL 扫描码
// 参数：ch = ASCII 字符
// 返回：SDL 扫描码（SDL_SCANCODE_*）
static int ascii_to_scancode(int ch) {
  if (isalpha(ch)) {                    // 字母 a-z, A-Z
    return SDL_SCANCODE_A + tolower(ch) - 'a';
  }

  if (ch >= '1' && ch <= '9') {         // 数字 1-9
    return SDL_SCANCODE_1 + ch - '1';
  }

  switch (ch) {                         // 特殊字符
    case '0': return SDL_SCANCODE_0;
    case ' ': return SDL_SCANCODE_SPACE;
    case '\n':
    case '\r': return SDL_SCANCODE_RETURN;
    case '\t': return SDL_SCANCODE_TAB;
    case 0x7f:                          // DEL
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

// inject_terminal_key: 注入终端按键
// 功能：模拟按下和释放一个键（发送给客户程序）
// 参数：scancode = 键盘扫描码
static void inject_terminal_key(uint8_t scancode) {
  if (scancode == SDL_SCANCODE_UNKNOWN) {
    return;
  }
  send_key(scancode, true);             // 发送按下事件
  send_key(scancode, false);            // 发送释放事件
}

// poll_terminal_input: 轮询终端输入
// 功能：从 stdin 读取字符，转换成按键事件
// 特殊处理：ESC 序列（方向键等）
static void poll_terminal_input(void) {
  if (!terminal_input_active) {
    return;
  }

  char buf[64];
  ssize_t nread = read(STDIN_FILENO, buf, sizeof(buf));
  // 非阻塞读取（立即返回）
  if (nread <= 0) {
    return;                             // 没有输入
  }

  for (ssize_t i = 0; i < nread; i ++) {
    unsigned char ch = (unsigned char)buf[i];

    // ESC 序列状态机（解析方向键等特殊键）
    if (terminal_esc_state == 1) {      // 收到 ESC 后
      if (ch == '[') {                  // ESC [ → 进入 CSI 序列
        terminal_esc_state = 2;
        continue;
      }
      terminal_esc_state = 0;
      inject_terminal_key(SDL_SCANCODE_ESCAPE);
    }
    else if (terminal_esc_state == 2) { // ESC [ 后
      switch (ch) {                     // 方向键：ESC[A/B/C/D
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

    if (ch == 0x1b) {                   // ESC（0x1b）
      terminal_esc_state = 1;
      continue;
    }

    inject_terminal_key(ascii_to_scancode(ch));
  }
}

// poll_sdl_events: 轮询 SDL 事件
// 功能：处理 SDL 窗口事件（键盘、关闭等）
static void poll_sdl_events(void) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {       // 逐个取出 SDL 事件队列中的事件
    switch (event.type) {
      case SDL_QUIT:                    // 关闭窗口
        exit(0);
      case SDL_KEYDOWN:                 // 按键按下
      case SDL_KEYUP: {                 // 按键释放
        if (event.key.repeat == 0) {    // 忽略重复事件（按住不放产生的）
          uint8_t k = event.key.keysym.scancode;
          bool is_keydown = (event.key.type == SDL_KEYDOWN);
          send_key(k, is_keydown);      // 发送按键事件给客户程序
        }
        break;
      }
      default: break;
    }
  }
}

// timer_sig_handler: 定时器信号处理函数
// 功能：每隔 10ms 被操作系统调用一次（SIGALRM 信号）
// 工作内容：增加 jiffy 计数、触发定时器中断、设置更新标志
static void timer_sig_handler(int signum) {
  (void)signum;                         // 忽略参数
  jiffy ++;                             // 滴答计数 +1
  timer_intr();                         // 触发定时器中断（设置 timer_intr_pending）

  device_update_flag = true;            // 设置设备更新标志
  if (jiffy % (TIMER_HZ / VGA_HZ) == 0) {
    // 每 2 次定时器中断（100Hz / 50Hz = 2）更新一次屏幕
    update_screen_flag = true;
  }

  int ret = setitimer(ITIMER_REAL, &it, NULL);
  // 重新设置定时器（一次性定时器，需要每次重新设置）
  Assert(ret == 0, "Can not set timer");
}

// device_update: 设备更新主函数
// 功能：检查设备更新标志，处理定时器中断和屏幕刷新
// 调用时机：cpu_exec 中每执行若干条指令调用一次
void device_update() {
  if (!device_update_flag) {            // 如果没有定时器中断
    poll_terminal_input();              // 轮询终端输入
    if ((++input_poll_budget & 0x3ff) == 0) {
      // 每 1024 次（0x3ff + 1）轮询一次 SDL 事件
      // 避免频繁轮询影响性能
      poll_sdl_events();
    }
    return;
  }
  input_poll_budget = 0;                // 重置预算
  device_update_flag = false;           // 清除标志

  (void)timer_try_raise_intr();         // 尝试触发定时器中断

  if (update_screen_flag) {             // 如果需要更新屏幕
    update_screen();                    // 刷新 VGA 显示
    update_screen_flag = false;
  }

  poll_terminal_input();                // 轮询终端输入
  poll_sdl_events();                    // 轮询 SDL 事件
}

// device_update_in_instr: 指令内设备更新
// 功能：在 rep 指令循环中调用，避免长时间阻塞导致设备无响应
// 返回：true = 触发了中断（需要暂停 rep），false = 继续执行
bool device_update_in_instr() {
  poll_terminal_input();
  poll_sdl_events();
  if (timer_try_raise_intr()) {         // 如果触发了定时器中断
    return true;                        // 返回 true，让 rep 暂停
  }
  if (!update_screen_flag) {
    return false;
  }
  update_screen();
  update_screen_flag = false;
  return false;
}

// sdl_clear_event_queue: 清空 SDL 事件队列
// 功能：丢弃所有待处理的 SDL 事件
// 用途：某些情况下需要清空事件队列（例如暂停后恢复）
void sdl_clear_event_queue() {
  SDL_Event event;
  while (SDL_PollEvent(&event));       // 逐个取出并丢弃
}

// init_device: 初始化所有设备
// 功能：NEMU 启动时调用，初始化所有模拟设备和定时器
void init_device() {
  init_serial();                        // 初始化串口
  init_timer();                         // 初始化定时器（RTC）
  init_vga();                           // 初始化 VGA 显示
  init_i8042();                         // 初始化键盘控制器
  init_terminal_input();                // 初始化终端键盘模式

  // 注册定时器信号处理函数
  struct sigaction s;
  memset(&s, 0, sizeof(s));
  s.sa_handler = timer_sig_handler;     // 信号处理函数
  int ret = sigaction(SIGALRM, &s, NULL);
  Assert(ret == 0, "Can not set signal handler");

  // 设置定时器：每 10ms 触发一次 SIGALRM 信号
  it.it_value.tv_sec = 0;               // 秒数 = 0
  it.it_value.tv_usec = 1000000 / TIMER_HZ;  // 微秒 = 10000（10ms）
  ret = setitimer(ITIMER_REAL, &it, NULL);
  Assert(ret == 0, "Can not set timer");
}
#else                                   // 没有定义 HAS_IOE

void init_device() {                    // 空实现（无设备支持）
}

#endif	/* HAS_IOE */

// ──────────────────────────────────────────────────────────────────────────────
// 设备事件循环工作流程：
//
// 1. init_device() 初始化：
//    - 初始化各个设备（串口、定时器、VGA、键盘）
//    - 设置 SIGALRM 信号处理函数（timer_sig_handler）
//    - 启动定时器（每 10ms 触发一次信号）
//
// 2. 定时器信号到达（每 10ms）：
//    timer_sig_handler() 执行：
//      jiffy++
//      timer_intr()（设置 timer_intr_pending = 1）
//      device_update_flag = true
//      每 2 次：update_screen_flag = true
//
// 3. CPU 执行指令：
//    cpu_exec() → exec_wrapper() → ...
//    每执行若干条指令，调用 device_update()
//
// 4. device_update() 检查标志：
//    if (device_update_flag):
//      timer_try_raise_intr()（触发时钟中断）
//      if (update_screen_flag): update_screen()
//      poll_terminal_input()
//      poll_sdl_events()
//
// 5. 时钟中断被触发：
//    cpu.INTR = true
//    → poll_intr() → raise_intr(32, ...) → 跳转到时钟中断处理程序
//
// 6. 循环往复
//
// ──────────────────────────────────────────────────────────────────────────────
// 定时器频率的权衡：
//
// TIMER_HZ = 100（每秒 100 次）：
//   优点：足够频繁，操作系统时间片调度精度高
//   缺点：频繁中断影响性能
//
// VGA_HZ = 50（每秒 50 次）：
//   优点：刷新率够用（人眼感知极限约 60Hz）
//   缺点：降低一半，减少 CPU 开销
//
// 真实系统：
//   - Linux 默认 HZ = 250 或 1000
//   - Windows 默认 HZ = 64 或 100
//   - 屏幕刷新率通常 60Hz 或更高
//
// ──────────────────────────────────────────────────────────────────────────────
