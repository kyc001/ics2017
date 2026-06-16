// keyboard.c - 键盘设备模拟（PA3 设备驱动）
// 功能：模拟 i8042 键盘控制器，提供键盘输入接口
// 工作原理：SDL 事件 → 按键队列 → 端口 I/O → 客户程序

#include "device/port-io.h"   // 端口 I/O 接口
#include "monitor/monitor.h"   // NEMU 状态定义
#include <SDL2/SDL.h>         // SDL 键盘扫描码定义

#define I8042_DATA_PORT 0x60  // i8042 数据端口地址（标准 x86 键盘端口）
#define KEYBOARD_IRQ 1        // 键盘 IRQ 号（中断请求线 1）

static uint32_t *i8042_data_port_base;
// i8042 数据端口的内存映射基址

// _KEYS 宏：定义所有支持的按键
// 用法：_KEYS(X) 会把每个按键名传给宏 X
// 这是一种 X 宏技巧（X-Macro），用于减少重复代码
#define _KEYS(_) \
  _(ESCAPE) _(F1) _(F2) _(F3) _(F4) _(F5) _(F6) _(F7) _(F8) _(F9) _(F10) _(F11) _(F12) \
_(GRAVE) _(1) _(2) _(3) _(4) _(5) _(6) _(7) _(8) _(9) _(0) _(MINUS) _(EQUALS) _(BACKSPACE) \
_(TAB) _(Q) _(W) _(E) _(R) _(T) _(Y) _(U) _(I) _(O) _(P) _(LEFTBRACKET) _(RIGHTBRACKET) _(BACKSLASH) \
_(CAPSLOCK) _(A) _(S) _(D) _(F) _(G) _(H) _(J) _(K) _(L) _(SEMICOLON) _(APOSTROPHE) _(RETURN) \
_(LSHIFT) _(Z) _(X) _(C) _(V) _(B) _(N) _(M) _(COMMA) _(PERIOD) _(SLASH) _(RSHIFT) \
_(LCTRL) _(APPLICATION) _(LALT) _(SPACE) _(RALT) _(RCTRL) \
_(UP) _(DOWN) _(LEFT) _(RIGHT) _(INSERT) _(DELETE) _(HOME) _(END) _(PAGEUP) _(PAGEDOWN)
// 包含所有常用按键：ESC、功能键、字母、数字、符号、方向键等

#define _KEY_NAME(k) _KEY_##k,
// 辅助宏：生成枚举名
// 例如：_KEY_NAME(A) 展开成 _KEY_A,

// 按键编号枚举
// 使用 X 宏技巧自动生成：_KEY_ESCAPE, _KEY_F1, _KEY_F2, ..., _KEY_A, _KEY_B, ...
enum {
  _KEY_NONE = 0,                        // 0 = 没有按键
  _KEYS(_KEY_NAME)                      // 1 = _KEY_ESCAPE, 2 = _KEY_F1, ...
};

#define XX(k) [concat(SDL_SCANCODE_, k)] = concat(_KEY_, k),
// 映射宏：SDL 扫描码 → 按键编号
// 例如：XX(A) 展开成 [SDL_SCANCODE_A] = _KEY_A,

// 键盘映射表：SDL 扫描码 → 按键编号
// 例如：keymap[SDL_SCANCODE_A] = _KEY_A
static uint32_t keymap[256] = {
  _KEYS(XX)                             // 用 X 宏自动填充整个映射表
};

#define KEY_QUEUE_LEN 1024              // 按键队列长度（循环队列）
static int key_queue[KEY_QUEUE_LEN];   // 按键队列（存放待处理的按键事件）
static int key_f = 0, key_r = 0;       // 队列头尾指针（f = front, r = rear）

#define KEYDOWN_MASK 0x8000             // 按下标志：bit 15 = 1 表示按下，0 表示释放
                                        // 例如：0x8001 = 按下 A 键，0x0001 = 释放 A 键

// send_key: 发送按键事件到队列
// 功能：把 SDL 按键事件转换成 AM（Abstract Machine）格式，放入队列
// 参数：scancode = SDL 扫描码，is_keydown = true 为按下，false 为释放
// 调用时机：device.c 的 poll_sdl_events 或 poll_terminal_input 中
void send_key(uint8_t scancode, bool is_keydown) {
  if (nemu_state == NEMU_RUNNING &&     // 只有 NEMU 运行时才处理按键
      keymap[scancode] != _KEY_NONE) {  // 只处理映射表中的按键（忽略不支持的键）
    uint32_t am_scancode = keymap[scancode] | (is_keydown ? KEYDOWN_MASK : 0);
    // AM 扫描码 = 按键编号 | 按下标志
    // 例如：按下 A 键 → 0x8000 | _KEY_A
    //       释放 A 键 → 0x0000 | _KEY_A
    key_queue[key_r] = am_scancode;     // 放入队列尾部
    key_r = (key_r + 1) % KEY_QUEUE_LEN;// 尾指针前进（循环）
    Assert(key_r != key_f, "key queue overflow!");
    // 断言：队列不能满（满了就是 bug，需要增大队列长度）
  }
}

// i8042_io_handler: i8042 端口 I/O 处理函数
// 功能：处理客户程序对 0x60 端口的读操作
// 参数：addr = 端口地址（0x60），len = 访问长度（4），is_write = 写操作标志
// 调用时机：客户程序执行 in $0x60, %eax 时
void i8042_io_handler(ioaddr_t addr, int len, bool is_write) {
  assert(!is_write);                    // 键盘端口是只读的（不支持写操作）
  assert(addr == I8042_DATA_PORT);      // 地址必须是 0x60
  assert(len == 4);                     // 长度必须是 4 字节

  if (key_f != key_r) {                 // 如果队列非空
    i8042_data_port_base[0] = key_queue[key_f];
    // 从队列头取出一个按键事件，写入端口缓冲区
    key_f = (key_f + 1) % KEY_QUEUE_LEN;// 头指针前进
  }
  else {                                // 如果队列为空
    i8042_data_port_base[0] = _KEY_NONE;// 返回 0（没有按键）
  }
}

// init_i8042: 初始化 i8042 键盘控制器
// 功能：注册端口 I/O 处理函数
// 调用时机：init_device() 中（NEMU 启动时）
void init_i8042() {
  i8042_data_port_base = add_pio_map(I8042_DATA_PORT, 4, i8042_io_handler);
  // 注册端口：0x60，大小 4 字节，处理函数 i8042_io_handler
  i8042_data_port_base[0] = _KEY_NONE;  // 初始状态：没有按键
}

// ──────────────────────────────────────────────────────────────────────────────
// 键盘事件流程：
//
// 1. 用户按下键盘（SDL 窗口或终端）：
//    SDL_KEYDOWN 事件 或 终端输入
//
// 2. device.c 捕获事件：
//    poll_sdl_events() 或 poll_terminal_input()
//    → send_key(scancode, is_keydown)
//
// 3. send_key 放入队列：
//    key_queue[key_r++] = am_scancode
//
// 4. 客户程序读取按键：
//    in $0x60, %eax
//    → i8042_io_handler()
//    → eax = key_queue[key_f++]（取出队列头）
//
// 5. 客户程序处理按键：
//    if (eax & 0x8000):  按下
//    else:               释放
//    key_code = eax & 0x7FFF
//
// ──────────────────────────────────────────────────────────────────────────────
// 客户程序读取按键示例（C 语言）：
//
// #define _KEY_A 18  // 假设 A 键编号是 18
// #define KEYDOWN_MASK 0x8000
//
// uint32_t read_key() {
//   uint32_t key;
//   asm volatile("in $0x60, %0" : "=a"(key));
//   return key;
// }
//
// void main() {
//   while (1) {
//     uint32_t key = read_key();
//     if (key == 0) continue;  // 没有按键
//
//     bool is_keydown = (key & KEYDOWN_MASK) != 0;
//     uint32_t keycode = key & ~KEYDOWN_MASK;
//
//     if (is_keydown && keycode == _KEY_A) {
//       printf("A key pressed\n");
//     }
//   }
// }
//
// ──────────────────────────────────────────────────────────────────────────────
// X 宏技巧解释：
//
// X 宏（X-Macro）是一种预处理器技巧，用于减少重复代码。
//
// 原理：
//   1. 定义一个宏，包含所有需要处理的数据（例如按键列表）
//   2. 用不同的 "处理宏" 多次展开这个列表
//   3. 每次展开生成不同的代码（枚举、映射表、字符串等）
//
// 例如，如果手写映射表：
//   keymap[SDL_SCANCODE_A] = _KEY_A;
//   keymap[SDL_SCANCODE_B] = _KEY_B;
//   ... （重复 60+ 行）
//
// 用 X 宏：
//   #define _KEYS(_) _(A) _(B) _(C) ...
//   #define XX(k) keymap[SDL_SCANCODE_##k] = _KEY_##k;
//   _KEYS(XX)  // 自动展开成上面 60+ 行
//
// 好处：
//   - 减少重复代码
//   - 修改列表时只需改一个地方
//   - 保证枚举、映射表、字符串等的一致性
//
// ──────────────────────────────────────────────────────────────────────────────
// i8042 键盘控制器：
//
// i8042 是 IBM PC/AT 的键盘控制器芯片，x86 架构的标准设备。
//
// 端口地址：
//   0x60：数据端口（读取按键扫描码）
//   0x64：命令/状态端口（发送命令、读取状态）
//
// NEMU 的简化实现：
//   - 只模拟 0x60 数据端口（足够运行简单的操作系统）
//   - 不模拟 0x64 命令端口（不支持键盘 LED、复位等高级功能）
//   - 使用循环队列缓冲按键事件（避免丢失）
//
// 真实的 i8042：
//   - 支持键盘和鼠标（PS/2 接口）
//   - 可以控制 A20 地址线（历史遗留）
//   - 可以重启计算机（0x64 端口写 0xFE）
//
// ──────────────────────────────────────────────────────────────────────────────
// 循环队列（Circular Queue）：
//
// 用两个指针实现：
//   key_f：队列头（front），指向下一个要读取的位置
//   key_r：队列尾（rear），指向下一个要写入的位置
//
// 判断队列状态：
//   空：key_f == key_r
//   满：(key_r + 1) % LEN == key_f
//
// 入队（enqueue）：
//   queue[key_r] = value;
//   key_r = (key_r + 1) % LEN;
//
// 出队（dequeue）：
//   value = queue[key_f];
//   key_f = (key_f + 1) % LEN;
//
// 优点：
//   - 空间利用率高（不浪费空间）
//   - O(1) 时间复杂度的入队/出队
//   - 适合生产者-消费者模型（SDL 事件 → 客户程序）
//
// ──────────────────────────────────────────────────────────────────────────────
// SDL 扫描码 vs x86 扫描码：
//
// SDL_SCANCODE：
//   - SDL 库定义的键盘扫描码（平台无关）
//   - 例如：SDL_SCANCODE_A = 4
//
// x86 扫描码（Set 1）：
//   - IBM PC/AT 键盘的硬件扫描码
//   - 例如：A 键 = 0x1E（按下），0x9E（释放）
//
// AM 扫描码（NEMU 使用）：
//   - 简化的扫描码格式
//   - 例如：A 键按下 = 0x8000 | keycode，释放 = keycode
//   - 优点：更容易处理（不需要区分 Set 1/2/3）
//
// 转换流程：
//   SDL_SCANCODE → keymap 查表 → AM 扫描码 → 客户程序
//
// ──────────────────────────────────────────────────────────────────────────────
