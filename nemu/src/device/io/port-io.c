// port-io.c - 端口 I/O（Port I/O）实现
// 功能：管理 I/O 端口地址空间，处理 in/out 指令
// 原理：x86 架构有独立的 I/O 地址空间（0-65535），用 in/out 指令访问

#include "common.h"
#include "device/port-io.h"   // 端口 I/O 接口定义

#define PORT_IO_SPACE_MAX 65536
// 端口 I/O 地址空间大小：64KB（16 位地址 = 0-65535）

#define NR_MAP 8              // 最多支持 8 个端口映射

/* "+ 3" is for hacking, see pio_read() below */
// + 3 是为了 hack：防止读取 4 字节时越界
// 例如：读取地址 65534（0xFFFE），长度 4 字节，会访问到 65537
// 多分配 3 字节，避免越界崩溃
static uint8_t pio_space[PORT_IO_SPACE_MAX + 3];
// 端口 I/O 空间：一块 64KB+3 的内存，模拟 I/O 端口

// PIO_t: 端口 I/O 映射的描述符
typedef struct {
  ioaddr_t low;               // 起始端口地址
  ioaddr_t high;              // 结束端口地址
  pio_callback_t callback;    // 回调函数（访问时触发）
} PIO_t;

static PIO_t maps[NR_MAP];    // 端口映射表
static int nr_map = 0;        // 当前已注册的映射数量

// pio_callback: 查找并触发端口回调函数
// 功能：遍历映射表，找到对应的回调函数并执行
// 参数：addr = 端口地址，len = 访问长度，is_write = 是否为写操作
// 调用时机：pio_read/pio_write 中
static void pio_callback(ioaddr_t addr, int len, bool is_write) {
  int i;
  for (i = 0; i < nr_map; i ++) {             // 遍历所有映射
    if (addr >= maps[i].low && addr + len - 1 <= maps[i].high) {
      // 如果访问的端口范围完全在某个映射内
      if (maps[i].callback != NULL) {         // 如果有回调函数
        maps[i].callback(addr, len, is_write);// 触发回调
      }
      return;                                 // 找到就返回（一个端口只属于一个设备）
    }
  }
  // 如果没有找到映射，什么都不做（访问未映射的端口被忽略）
}

/* device interface */
// add_pio_map: 注册一个端口 I/O 映射（设备接口）
// 功能：为设备分配端口地址范围
// 参数：addr = 起始端口地址，len = 长度，callback = 访问回调函数
// 返回：对应的内存空间指针（设备可以直接读写）
// 调用时机：设备初始化时（例如 init_serial）
void* add_pio_map(ioaddr_t addr, int len, pio_callback_t callback) {
  assert(nr_map < NR_MAP);                    // 断言：映射数量未满
  assert(addr + len <= PORT_IO_SPACE_MAX);    // 断言：地址范围有效
  maps[nr_map].low = addr;                    // 记录起始地址
  maps[nr_map].high = addr + len - 1;         // 记录结束地址
  maps[nr_map].callback = callback;           // 记录回调函数
  nr_map ++;                                  // 映射数量 +1
  return pio_space + addr;                    // 返回对应的内存空间指针
}

// pio_read_common: 端口读取通用函数
// 功能：从端口读取数据
// 参数：addr = 端口地址，len = 读取长度（1/2/4 字节）
// 返回：读取的数据
// 工作流程：
//   1. 触发回调（让设备准备数据）
//   2. 从 pio_space 读取数据
static inline uint32_t pio_read_common(ioaddr_t addr, int len) {
  assert(addr + len - 1 < PORT_IO_SPACE_MAX); // 断言：地址范围有效
  pio_callback(addr, len, false);		// prepare data to read
  // 先触发回调（false = 读操作）
  // 设备的回调函数会把数据写入 pio_space[addr]
  switch (len) {                              // 根据长度读取
    case 4: return *(uint32_t *)(pio_space + addr);
    // 4 字节：读取 32 位
    case 2: return *(uint16_t *)(pio_space + addr);
    // 2 字节：读取 16 位
    case 1: return *(uint8_t *)(pio_space + addr);
    // 1 字节：读取 8 位
    default: assert(0);
  }
}

// pio_write_common: 端口写入通用函数
// 功能：向端口写入数据
// 参数：addr = 端口地址，data = 要写入的数据，len = 写入长度
// 工作流程：
//   1. 把数据写入 pio_space
//   2. 触发回调（让设备处理数据）
static inline void pio_write_common(ioaddr_t addr, uint32_t data, int len) {
  assert(addr + len - 1 < PORT_IO_SPACE_MAX); // 断言：地址范围有效
  switch (len) {                              // 根据长度写入
    case 4: *(uint32_t *)(pio_space + addr) = data; break;
    // 4 字节：写入 32 位
    case 2: *(uint16_t *)(pio_space + addr) = data; break;
    // 2 字节：写入 16 位
    case 1: *(uint8_t *)(pio_space + addr) = data; break;
    // 1 字节：写入 8 位
    default: assert(0);
  }
  pio_callback(addr, len, true);              // 触发回调（true = 写操作）
  // 设备的回调函数从 pio_space[addr] 读取数据并处理
}

// pio_read_l/w/b: 端口读取接口（供 in 指令调用）
// l = long (4 字节), w = word (2 字节), b = byte (1 字节)
uint32_t pio_read_l(ioaddr_t addr) {
  return pio_read_common(addr, 4);
}

uint32_t pio_read_w(ioaddr_t addr) {
  return pio_read_common(addr, 2);
}

uint32_t pio_read_b(ioaddr_t addr) {
  return pio_read_common(addr, 1);
}

// pio_write_l/w/b: 端口写入接口（供 out 指令调用）
void pio_write_l(ioaddr_t addr, uint32_t data) {
  pio_write_common(addr, data, 4);
}

void pio_write_w(ioaddr_t addr, uint32_t data) {
  pio_write_common(addr, data, 2);
}

void pio_write_b(ioaddr_t addr, uint32_t data) {
  pio_write_common(addr, data, 1);
}

// ──────────────────────────────────────────────────────────────────────────────
// 端口 I/O 工作原理：
//
// x86 架构有两个独立的地址空间：
//   1. 内存地址空间：0-4GB（32 位），用 mov 指令访问
//   2. I/O 地址空间：0-65535（16 位），用 in/out 指令访问
//
// 客户程序执行：in $0x60, %al
//   → exec_in() 被调用（在 system.c）
//   → pio_read_b(0x60)
//   → pio_callback(0x60, 1, false)
//   → i8042_io_handler(0x60, 1, false)（键盘设备回调）
//   → 键盘设备把按键数据写入 pio_space[0x60]
//   → 返回 pio_space[0x60] 给客户程序
//
// 客户程序执行：out %al, $0x3F8
//   → exec_out() 被调用
//   → pio_write_b(0x3F8, al)
//   → pio_space[0x3F8] = al
//   → pio_callback(0x3F8, 1, true)
//   → serial_io_handler(0x3F8, 1, true)（串口设备回调）
//   → 串口设备从 pio_space[0x3F8] 读取字符并输出
//
// ──────────────────────────────────────────────────────────────────────────────
// 端口映射表示例：
//
// 假设有 3 个设备：
//   串口（COM1）：0x3F8-0x3FF（8 字节）
//   键盘（i8042）：0x60-0x67（8 字节）
//   定时器（RTC）：0x48-0x4B（4 字节）
//
// maps[] 数组：
//   [0] = { low: 0x3F8, high: 0x3FF, callback: serial_io_handler }
//   [1] = { low: 0x60, high: 0x67, callback: i8042_io_handler }
//   [2] = { low: 0x48, high: 0x4B, callback: rtc_io_handler }
//
// 当客户程序执行：out %al, $0x3F8
//   → pio_write_b(0x3F8, al)
//   → pio_space[0x3F8] = al
//   → pio_callback 找到 maps[0]（0x3F8 在范围内）
//   → serial_io_handler(0x3F8, 1, true)
//   → 串口输出字符
//
// ──────────────────────────────────────────────────────────────────────────────
// 为什么读操作先触发回调，写操作后触发回调？
//
// 读操作：in $0x60, %al
//   1. 先调用回调：i8042_io_handler(0x60, 1, false)
//      → 设备把数据准备好，写入 pio_space[0x60]
//   2. 再读取：al = pio_space[0x60]
//
// 写操作：out %al, $0x3F8
//   1. 先写入：pio_space[0x3F8] = al
//   2. 再调用回调：serial_io_handler(0x3F8, 1, true)
//      → 设备从 pio_space[0x3F8] 读取数据并处理
//
// 这样设计的原因：
//   - 读操作：设备需要先准备数据（例如从队列取出）
//   - 写操作：设备需要从 pio_space 读取客户程序写入的数据
//
// ──────────────────────────────────────────────────────────────────────────────
// 常见的 I/O 端口地址（x86 标准）：
//
// 端口地址       | 设备
// ───────────────┼──────────────────────────────────────
// 0x20-0x21      | 主 PIC（中断控制器）
// 0xA0-0xA1      | 从 PIC
// 0x40-0x43      | PIT（可编程间隔定时器）
// 0x60-0x64      | i8042（键盘控制器）
// 0x70-0x71      | CMOS/RTC（实时时钟）
// 0x3F8-0x3FF    | COM1（串口 1）
// 0x2F8-0x2FF    | COM2（串口 2）
// 0x378-0x37F    | LPT1（并口 1）
// 0x1F0-0x1F7    | IDE 主通道（硬盘控制器）
// 0x170-0x177    | IDE 从通道
// 0x3D4-0x3D5    | VGA 寄存器
// 0xCF8-0xCFF    | PCI 配置空间
//
// ──────────────────────────────────────────────────────────────────────────────
// 端口 I/O vs MMIO 的选择：
//
// 端口 I/O 的优点：
//   - 独立地址空间，不占用内存地址
//   - 指令简单明确（in/out）
//   - 传统兼容性好
//
// 端口 I/O 的缺点：
//   - 地址空间小（只有 64KB）
//   - 需要特权（IOPL 权限）
//   - 不能用 DMA 直接访问
//
// MMIO 的优点：
//   - 地址空间大（可以映射 GB 级设备内存）
//   - 用普通内存指令（mov/add/sub）
//   - DMA 天然支持
//   - 可以用分页保护
//
// 现代趋势：
//   - 传统设备（串口、键盘）：端口 I/O
//   - 现代设备（显卡、网卡）：MMIO
//   - x86 同时支持两种方式
//
// ──────────────────────────────────────────────────────────────────────────────
// "+ 3" hack 的原因：
//
// 问题：如果客户程序执行 in $0xFFFE, %eax
//   → 读取 4 字节：0xFFFE, 0xFFFF, 0x10000, 0x10001
//   → 但 pio_space 只有 65536 字节（0-65535）
//   → 访问 pio_space[65536] 越界崩溃！
//
// 解决：多分配 3 字节（PORT_IO_SPACE_MAX + 3）
//   → pio_space[65536], pio_space[65537], pio_space[65538] 都是合法的
//   → 避免崩溃
//
// 为什么是 + 3 而不是 + 4：
//   → 最大访问地址 = 65535 + 3 = 65538
//   → 最大访问长度 = 4 字节
//   → 65535 + 4 - 1 = 65538（刚好）
//
// 这是一个"防御性编程"的例子，虽然正常情况不会访问 0xFFFE+4，
// 但如果客户程序有 bug 或恶意代码，至少不会让 NEMU 崩溃。
//
// ──────────────────────────────────────────────────────────────────────────────
