// timer.c - 定时器设备模拟（PA3 设备驱动）
// 功能：模拟硬件定时器，提供时钟中断和实时时钟（RTC）功能
// 用途：支持操作系统的时钟中断、获取系统运行时间

#include "device/port-io.h"   // 端口 I/O 接口
#include "monitor/monitor.h"   // NEMU 状态定义
#include <sys/time.h>         // gettimeofday() 函数

#define RTC_PORT 0x48   // Note that this is not the standard
// RTC 端口地址：0x48（实时时钟，Real-Time Clock）
// 注意：这不是标准的 x86 RTC 端口（标准是 0x70/0x71）
// NEMU 为了简化，使用自定义的端口地址

static volatile int timer_intr_pending = 0;
// 定时器中断待处理标志
// volatile：告诉编译器这个变量可能被其他线程修改（信号处理函数）
// 0 = 没有待处理的中断，1 = 有待处理的中断

static struct timeval boot_time;
// NEMU 启动时间（用于计算运行时长）
// timeval 结构体包含：tv_sec（秒），tv_usec（微秒）

// timer_intr: 定时器中断处理函数
// 功能：设置中断待处理标志（由外部定时器信号调用）
// 工作原理：
//   - 操作系统会定期发送信号（SIGALRM）给 NEMU 进程
//   - 信号处理函数调用这个函数
//   - 设置 timer_intr_pending = 1
//   - CPU 在执行指令的间隙检查这个标志，触发时钟中断
void timer_intr() {
  if (nemu_state == NEMU_RUNNING) {   // 只有在 NEMU 运行时才设置中断
                                      // 如果 NEMU 停在调试器，不触发中断
    timer_intr_pending = 1;           // 设置中断待处理标志
  }
}

// timer_try_raise_intr: 尝试触发定时器中断
// 功能：检查是否有待处理的定时器中断，如果有就触发
// 返回：true = 触发了中断，false = 没有待处理的中断
// 调用时机：device_update() 中（每执行若干条指令检查一次）
bool timer_try_raise_intr() {
  if (timer_intr_pending) {           // 如果有待处理的中断
    timer_intr_pending = 0;           // 清除标志（避免重复触发）
    extern void dev_raise_intr(void); // 声明：设备中断触发函数（在 intr.c）
    dev_raise_intr();                 // 触发设备中断（设置 cpu.INTR = true）
                                      // CPU 会在下一次 poll_intr 时响应中断
    return true;                      // 返回"已触发中断"
  }
  return false;                       // 没有待处理的中断
}

static uint32_t *rtc_port_base;       // RTC 端口的内存映射基址
                                      // 客户程序通过 in/out 指令访问这个地址

// get_elapsed_ms: 获取 NEMU 运行时长（毫秒）
// 功能：计算从 NEMU 启动到现在经过了多少毫秒
// 返回：运行时长（毫秒）
static uint32_t get_elapsed_ms(void) {
  struct timeval now;                 // 当前时间
  gettimeofday(&now, NULL);           // 获取当前时间（系统调用）

  int64_t sec = (int64_t)now.tv_sec - (int64_t)boot_time.tv_sec;
  // 秒数差 = 当前秒数 - 启动时秒数

  int64_t usec = (int64_t)now.tv_usec - (int64_t)boot_time.tv_usec;
  // 微秒数差 = 当前微秒 - 启动时微秒

  if (usec < 0) {                     // 如果微秒数差为负（借位）
    sec --;                           // 秒数减 1
    usec += 1000000;                  // 微秒数加 1000000（1 秒 = 1000000 微秒）
  }
  // 例如：now = 10.2 秒，boot = 9.8 秒
  //       usec = 200000 - 800000 = -600000（借位）
  //       sec = 10 - 9 - 1 = 0
  //       usec = -600000 + 1000000 = 400000
  //       结果 = 0.4 秒（正确）

  return (uint32_t)((uint64_t)sec * 1000 + (uint64_t)(usec + 500) / 1000);
  // 转换成毫秒：sec * 1000 + usec / 1000
  // usec + 500：四舍五入（加上半个毫秒再除以 1000）
  // 例如：0.4 秒 = 0 * 1000 + (400000 + 500) / 1000 = 400 毫秒
}

// rtc_io_handler: RTC 端口 I/O 处理函数
// 功能：处理客户程序对 RTC 端口的读写
// 参数：addr = 端口地址，len = 访问长度，is_write = 是否为写操作
// 调用时机：客户程序执行 in/out 指令访问 RTC_PORT 时
void rtc_io_handler(ioaddr_t addr, int len, bool is_write) {
  (void)addr;                         // 忽略地址参数（只有一个端口）
  (void)len;                          // 忽略长度参数（固定 4 字节）
  if (!is_write) {                    // 如果是读操作（in 指令）
    rtc_port_base[0] = get_elapsed_ms();
    // 把当前运行时长（毫秒）写入端口缓冲区
    // 客户程序执行 in 指令时会读取这个值
  }
  // 写操作（out 指令）被忽略（RTC 是只读的）
}

// init_timer: 初始化定时器
// 功能：记录启动时间，注册 RTC 端口
// 调用时机：init_device() 中（NEMU 启动时）
void init_timer() {
  gettimeofday(&boot_time, NULL);     // 记录 NEMU 启动时间
                                      // 后续 get_elapsed_ms 会用这个时间计算时长
  rtc_port_base = add_pio_map(RTC_PORT, 4, rtc_io_handler);
  // 注册 RTC 端口：
  //   RTC_PORT = 0x48：端口地址
  //   4：端口大小（4 字节 = 32 位）
  //   rtc_io_handler：I/O 处理函数
  // 返回：端口缓冲区的指针（用于存放读取的数据）
}

// ──────────────────────────────────────────────────────────────────────────────
// 定时器中断工作流程：
//
// 1. NEMU 启动时设置定时器信号（在 device.c 的 init_device 中）：
//    struct itimerval it = { ... };
//    setitimer(ITIMER_VIRTUAL, &it, NULL);  // 设置定时器，每 XX 毫秒触发一次
//    signal(SIGVTALRM, timer_intr);         // 注册信号处理函数
//
// 2. 操作系统定期发送 SIGVTALRM 信号给 NEMU 进程：
//    timer_intr() 被调用
//    → timer_intr_pending = 1
//
// 3. CPU 执行指令时，device_update() 检查中断：
//    timer_try_raise_intr()
//    → 如果 timer_intr_pending = 1
//    → dev_raise_intr()（设置 cpu.INTR = true）
//
// 4. exec_wrapper 的 poll_intr() 检查 cpu.INTR：
//    如果 cpu.INTR = true 且 IF = 1
//    → raise_intr(32, cpu.eip)（触发 32 号中断 = 时钟中断）
//
// 5. raise_intr 执行中断流程：
//    push EFLAGS; push CS; push EIP
//    从 IDT[32] 读取时钟中断处理程序地址
//    跳转到时钟中断处理程序（操作系统内核代码）
//
// ──────────────────────────────────────────────────────────────────────────────
// RTC（实时时钟）使用方法：
//
// 客户程序（汇编）：
//   mov $0x48, %dx          # RTC 端口地址
//   in %dx, %eax            # 从 RTC 读取当前时间（毫秒）
//   # 现在 eax = NEMU 运行时长（毫秒）
//
// 客户程序（C 语言）：
//   static inline uint32_t get_uptime() {
//     uint32_t ms;
//     asm volatile("in $0x48, %0" : "=a"(ms));
//     return ms;
//   }
//
//   void sleep_ms(uint32_t ms) {
//     uint32_t start = get_uptime();
//     while (get_uptime() - start < ms);  // 忙等待
//   }
//
// 用途：
//   - 测量程序运行时间
//   - 实现延时函数
//   - 性能分析（计时）
//
// ──────────────────────────────────────────────────────────────────────────────
// 端口 I/O vs 内存映射 I/O：
//
// 端口 I/O（Port I/O，PIO）：
//   - 使用专门的 in/out 指令
//   - 独立的地址空间（0-65535）
//   - x86 的传统方式
//   - 例如：in $0x60, %al（从键盘控制器读取）
//
// 内存映射 I/O（Memory-Mapped I/O，MMIO）：
//   - 使用普通的 mov 指令
//   - 共享内存地址空间
//   - 现代设备的主流方式
//   - 例如：mov 0xB8000, %eax（访问 VGA 显存）
//
// NEMU 的实现：
//   - 端口 I/O：通过 add_pio_map 注册，in/out 指令触发回调
//   - MMIO：通过 add_mmio_map 注册，vaddr_read/write 检查地址范围
//
// ──────────────────────────────────────────────────────────────────────────────
// 时钟中断的作用：
//
// 操作系统依赖时钟中断实现：
//
// 1. 进程调度（Process Scheduling）：
//    - 每个时钟中断，操作系统检查当前进程的时间片是否用完
//    - 如果用完，保存现场，切换到下一个进程
//    - 这就是"分时多任务"的基础
//
// 2. 系统时间更新：
//    - 每次时钟中断，系统时间 +1 tick
//    - 用于实现 time()、gettimeofday() 等系统调用
//
// 3. 定时器服务：
//    - 用户程序可以设置定时器（alarm、sleep）
//    - 时钟中断时检查定时器是否到期
//
// 4. 性能统计：
//    - 记录每个进程的 CPU 使用时间
//    - 用于性能分析和资源管理
//
// 没有时钟中断的后果：
//   - 一个进程可以永久占用 CPU（无法强制切换）
//   - 系统时间不更新（时钟停止）
//   - 定时器失效
//   - 操作系统失去对系统的控制权
//
// ──────────────────────────────────────────────────────────────────────────────
// 为什么用 volatile：
//
// timer_intr_pending 被声明为 volatile，原因：
//
//   1. timer_intr() 在信号处理函数中执行（类似中断上下文）
//   2. 主程序在 timer_try_raise_intr() 中读取这个变量
//   3. 编译器可能把变量缓存在寄存器中，导致读到旧值
//   4. volatile 告诉编译器：每次都从内存重新读取，不要缓存
//
// 例如，如果没有 volatile：
//   while (!timer_intr_pending);  // 等待中断
//   // 编译器可能优化成：
//   if (!timer_intr_pending) while (1);  // 死循环！
//   // 因为编译器认为 timer_intr_pending 不会改变
//
// 有了 volatile：
//   while (!timer_intr_pending);  // 每次都从内存读取
//   // 信号处理函数修改内存中的值，循环能正常退出
//
// ──────────────────────────────────────────────────────────────────────────────
