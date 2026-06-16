// serial.c - 串口设备模拟（PA3 设备驱动）
// 功能：模拟 8250 UART 串口，提供字符输出接口
// 用途：客户程序通过串口输出文本（类似 printf）

#include "common.h"
#include "device/port-io.h"   // 端口 I/O 接口

/* http://en.wikibooks.org/wiki/Serial_Programming/8250_UART_Programming */
// 参考：8250 UART 编程手册

#define SERIAL_PORT 0x3F8     // 串口基地址（COM1 的标准端口）
                              // COM1 = 0x3F8, COM2 = 0x2F8, COM3 = 0x3E8, COM4 = 0x2E8
#define CH_OFFSET 0           // 字符寄存器偏移（相对于基地址）
#define LSR_OFFSET 5          // 线路状态寄存器偏移（Line Status Register）

static uint8_t *serial_port_base;
// 串口端口的内存映射基址

// serial_io_handler: 串口端口 I/O 处理函数
// 功能：处理客户程序对串口的读写操作
// 参数：addr = 端口地址，len = 访问长度，is_write = 是否为写操作
// 调用时机：客户程序执行 out 指令访问串口时
void serial_io_handler(ioaddr_t addr, int len, bool is_write) {
  if (!is_write) {                      // 如果是读操作
    return;                             // 不处理（客户程序通常只写串口）
  }

  assert(len == 1);                     // 串口每次只能传输 1 字节
  if (addr == SERIAL_PORT + CH_OFFSET) {// 如果访问字符寄存器（0x3F8）
    char c = serial_port_base[CH_OFFSET];
    // 读取客户程序写入的字符
    /* We bind the serial port with the host stdout in NEMU. */
    // NEMU 把串口绑定到宿主机的 stdout（标准输出）
    putc(c, stdout);                    // 输出字符到宿主机终端
                                        // 这样客户程序的串口输出会显示在 NEMU 的终端上
    if (c == '\n') {                    // 如果是换行符
      fflush(stdout);                   // 立即刷新缓冲区（确保输出可见）
    }
  }
  // 其他端口（例如波特率设置、中断使能等）被忽略
}

// init_serial: 初始化串口
// 功能：注册串口端口，设置初始状态
// 调用时机：init_device() 中（NEMU 启动时）
void init_serial() {
  serial_port_base = add_pio_map(SERIAL_PORT, 8, serial_io_handler);
  // 注册串口端口：
  //   SERIAL_PORT = 0x3F8：基地址
  //   8：端口大小（8 字节，0x3F8-0x3FF）
  //   serial_io_handler：I/O 处理函数
  serial_port_base[LSR_OFFSET] = 0x20; /* the status is always free */
  // 设置线路状态寄存器（LSR）= 0x20
  // bit 5 = 1：发送保持寄存器空（Transmit Holding Register Empty）
  // 表示串口总是"就绪"，可以随时接收新字符
  // 简化实现：不模拟真实的发送延迟
}

// ──────────────────────────────────────────────────────────────────────────────
// 8250 UART 寄存器布局（简化）：
//
// 偏移 | 寄存器名                    | 读/写 | 说明
// ─────┼────────────────────────────┼───────┼─────────────────────────────────
//  +0  | THR (Transmit Holding Reg) | 写    | 发送缓冲区（写入字符）
//  +0  | RBR (Receive Buffer Reg)   | 读    | 接收缓冲区（读取字符）
//  +1  | IER (Interrupt Enable Reg) | 读写  | 中断使能
//  +2  | IIR (Interrupt ID Reg)     | 读    | 中断识别
//  +2  | FCR (FIFO Control Reg)     | 写    | FIFO 控制
//  +3  | LCR (Line Control Reg)     | 读写  | 线路控制（波特率、数据位等）
//  +4  | MCR (Modem Control Reg)    | 读写  | 调制解调器控制
//  +5  | LSR (Line Status Reg)      | 读    | 线路状态（是否就绪、是否有错误）
//  +6  | MSR (Modem Status Reg)     | 读    | 调制解调器状态
//  +7  | SCR (Scratch Reg)          | 读写  | 临时寄存器
//
// NEMU 的简化：
//   - 只实现 +0（字符寄存器）和 +5（状态寄存器）
//   - 其他寄存器被忽略（足够运行简单的操作系统）
//
// ──────────────────────────────────────────────────────────────────────────────
// 客户程序使用串口示例（汇编）：
//
// ; 检查串口是否就绪
// wait_ready:
//   mov dx, 0x3FD           ; LSR 地址 = 0x3F8 + 5
//   in al, dx               ; 读取状态
//   and al, 0x20            ; 检查 bit 5（THR Empty）
//   jz wait_ready           ; 如果为 0，继续等待
//
// ; 发送字符 'A'
//   mov dx, 0x3F8           ; 字符寄存器地址
//   mov al, 'A'             ; 要发送的字符
//   out dx, al              ; 输出到串口
//
// ──────────────────────────────────────────────────────────────────────────────
// 客户程序使用串口示例（C 语言）：
//
// #define SERIAL_PORT 0x3F8
// #define LSR_PORT (SERIAL_PORT + 5)
//
// // 等待串口就绪
// static void serial_wait() {
//   uint8_t status;
//   do {
//     asm volatile("in %1, %0" : "=a"(status) : "d"(LSR_PORT));
//   } while ((status & 0x20) == 0);
// }
//
// // 发送一个字符
// void serial_putc(char c) {
//   serial_wait();
//   asm volatile("out %0, %1" : : "a"(c), "d"(SERIAL_PORT));
// }
//
// // 发送字符串
// void serial_puts(const char *s) {
//   while (*s) {
//     serial_putc(*s++);
//   }
// }
//
// // 使用示例
// void main() {
//   serial_puts("Hello from NEMU!\n");
// }
//
// ──────────────────────────────────────────────────────────────────────────────
// 串口的用途：
//
// 1. 调试输出：
//    - 在操作系统内核开发中，printf 可能还不可用
//    - 串口是最简单、最可靠的输出方式
//    - 例如：Linux 内核的 early_printk 就用串口
//
// 2. 日志记录：
//    - 把日志输出到串口，保存到文件
//    - 例如：NEMU 可以用 ./nemu program > log.txt 保存串口输出
//
// 3. 远程终端：
//    - 真实硬件中，串口可以连接到另一台计算机
//    - 实现远程控制台（例如服务器的串口控制台）
//
// 4. 嵌入式系统：
//    - 嵌入式设备通常没有显示器
//    - 串口是主要的交互接口
//
// ──────────────────────────────────────────────────────────────────────────────
// NEMU 的串口绑定：
//
// 串口输出 → putc(c, stdout) → 宿主机终端
//
// 例如：
//   客户程序：out $0x3F8, 'H'
//   NEMU：putc('H', stdout)
//   宿主机终端：显示 'H'
//
// 这样设计的好处：
//   - 简单：不需要创建虚拟终端窗口
//   - 直观：客户程序的输出直接显示在运行 NEMU 的终端上
//   - 易于重定向：./nemu program > output.txt
//
// 缺点：
//   - 客户程序的输出和 NEMU 的调试输出混在一起
//   - 可以用 Log() 输出到日志文件，用串口输出客户程序的内容
//
// ──────────────────────────────────────────────────────────────────────────────
// LSR（线路状态寄存器）bit 定义：
//
// Bit | 名称 | 说明
// ────┼──────┼──────────────────────────────────────
//  0  | DR   | Data Ready（接收缓冲区有数据）
//  1  | OE   | Overrun Error（数据溢出）
//  2  | PE   | Parity Error（奇偶校验错误）
//  3  | FE   | Framing Error（帧错误）
//  4  | BI   | Break Interrupt（中断信号）
//  5  | THRE | Transmit Holding Register Empty（发送缓冲区空）← 重要
//  6  | TEMT | Transmitter Empty（发送器空）
//  7  | FIFO | FIFO Error（FIFO 错误）
//
// NEMU 设置 LSR = 0x20（bit 5 = 1）：
//   - 表示串口总是就绪，可以立即发送
//   - 客户程序不需要等待（简化实现）
//
// 真实硬件：
//   - 串口有波特率限制（例如 9600 bps = 每秒 960 字节）
//   - 发送一个字符需要时间，LSR bit 5 会变成 0
//   - 客户程序必须轮询 LSR，等待 bit 5 变回 1
//
// ──────────────────────────────────────────────────────────────────────────────
// 历史背景：为什么是 0x3F8？
//
// IBM PC 的 I/O 端口分配（1981 年）：
//   0x3F8-0x3FF：COM1（第一个串口）
//   0x2F8-0x2FF：COM2（第二个串口）
//   0x3E8-0x3EF：COM3
//   0x2E8-0x2EF：COM4
//
// 这个地址分配一直沿用至今，成为 x86 架构的标准。
// NEMU 模拟 COM1，所以用 0x3F8。
//
// ──────────────────────────────────────────────────────────────────────────────
