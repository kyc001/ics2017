// mmio.c - 内存映射 I/O（Memory-Mapped I/O）实现
// 功能：管理 MMIO 地址空间，拦截对设备寄存器的内存访问
// 原理：某些物理地址不对应真实的 RAM，而是映射到设备寄存器

#include "common.h"
#include "device/mmio.h"      // MMIO 接口定义

#define MMIO_SPACE_MAX (512 * 1024)
// MMIO 空间池的最大大小：512KB
// 这是所有 MMIO 设备共享的内存池

#define NR_MAP 8              // 最多支持 8 个 MMIO 映射区域

static uint8_t mmio_space_pool[MMIO_SPACE_MAX];
// MMIO 空间池：一块连续的内存，分配给各个 MMIO 设备
// 例如：VGA 显存占用 512KB，从这里分配

static uint32_t mmio_space_free_index = 0;
// 空闲空间的起始索引（下一次分配的位置）

// MMIO_t: MMIO 映射区域的描述符
typedef struct {
  paddr_t low;                // 起始物理地址
  paddr_t high;               // 结束物理地址
  uint8_t *mmio_space;        // 对应的内存空间指针（在 mmio_space_pool 中）
  mmio_callback_t callback;   // 回调函数（访问时触发）
} MMIO_t;

static MMIO_t maps[NR_MAP];   // MMIO 映射表
static int nr_map = 0;        // 当前已注册的映射数量

/* device interface */
// add_mmio_map: 注册一个 MMIO 映射区域（设备接口）
// 功能：为设备分配 MMIO 地址空间
// 参数：addr = 起始物理地址，len = 长度，callback = 访问回调函数
// 返回：分配的内存空间指针（设备可以直接读写）
// 调用时机：设备初始化时（例如 init_vga）
void* add_mmio_map(paddr_t addr, int len, mmio_callback_t callback) {
  assert(nr_map < NR_MAP);                    // 断言：映射数量未满
  assert(mmio_space_free_index + len <= MMIO_SPACE_MAX);
  // 断言：空间池有足够的剩余空间

  uint8_t *space_base = &mmio_space_pool[mmio_space_free_index];
  // 从空间池中分配 len 字节
  maps[nr_map].low = addr;                    // 记录起始地址
  maps[nr_map].high = addr + len - 1;         // 记录结束地址
  maps[nr_map].mmio_space = space_base;       // 记录内存空间指针
  maps[nr_map].callback = callback;           // 记录回调函数
  nr_map ++;                                  // 映射数量 +1
  mmio_space_free_index += len;               // 更新空闲空间索引
  return space_base;                          // 返回分配的空间指针
}

/* bus interface */
// is_mmio: 检查物理地址是否为 MMIO（总线接口）
// 功能：判断给定的物理地址是否落在某个 MMIO 区域内
// 参数：addr = 物理地址
// 返回：映射编号（0-7），或 -1（不是 MMIO）
// 调用时机：paddr_read/paddr_write 中（在 memory.h）
int is_mmio(paddr_t addr) {
  int i;
  for (i = 0; i < nr_map; i ++) {             // 遍历所有映射
    if (addr >= maps[i].low && addr <= maps[i].high) {
      // 如果地址在某个映射的范围内
      return i;                               // 返回映射编号
    }
  }
  return -1;                                  // 不是 MMIO 地址
}

// mmio_read: 读取 MMIO 地址（总线接口）
// 功能：从 MMIO 区域读取数据，并触发回调函数
// 参数：addr = 物理地址，len = 读取长度（1-4 字节），map_NO = 映射编号
// 返回：读取的数据
// 调用时机：paddr_read 检测到 MMIO 后调用
uint32_t mmio_read(paddr_t addr, int len, int map_NO) {
  assert(len >= 1 && len <= 4);               // 长度必须是 1-4 字节
  MMIO_t *map = &maps[map_NO];                // 获取映射描述符
  uint32_t data = *(uint32_t *)(map->mmio_space + (addr - map->low))
    & (~0u >> ((4 - len) << 3));
  // 从 MMIO 空间读取数据：
  //   map->mmio_space + (addr - map->low) = 对应的内存位置
  //   *(uint32_t *) = 读取 4 字节
  //   & (~0u >> ((4 - len) << 3)) = 截取低 len 字节
  //     例如：len=2 → 右移 16 位 → 0x0000FFFF → 只保留低 2 字节
  if (map->callback != NULL) {                // 如果有回调函数
    map->callback(addr, len, false);          // 触发回调（false = 读操作）
  }
  return data;                                // 返回读取的数据
}

// mmio_write: 写入 MMIO 地址（总线接口）
// 功能：向 MMIO 区域写入数据，并触发回调函数
// 参数：addr = 物理地址，len = 写入长度，data = 要写入的数据，map_NO = 映射编号
// 调用时机：paddr_write 检测到 MMIO 后调用
void mmio_write(paddr_t addr, int len, uint32_t data, int map_NO) {
  assert(len >= 1 && len <= 4);
  MMIO_t *map = &maps[map_NO];               // 获取映射描述符

  uint8_t *p = map->mmio_space + (addr - map->low);
  // p = 对应的内存位置
  uint8_t *p_data = (uint8_t *)&data;         // 把 data 当作字节数组

  switch (len) {                              // 按字节写入（小端序）
    case 4: p[3] = p_data[3];                 // 写入第 4 字节
    case 3: p[2] = p_data[2];                 // 写入第 3 字节
    case 2: p[1] = p_data[1];                 // 写入第 2 字节
    case 1: p[0] = p_data[0]; break;          // 写入第 1 字节
  }
  // 注意：这里用 fallthrough（没有 break），巧妙地处理不同长度

  if (map->callback != NULL) {                // 如果有回调函数
    map->callback(addr, len, true);           // 触发回调（true = 写操作）
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// MMIO（内存映射 I/O）工作原理：
//
// 传统方式（端口 I/O）：
//   设备寄存器 ← in/out 指令 → 独立的 I/O 地址空间（0-65535）
//
// MMIO 方式：
//   设备寄存器 ← mov 指令 → 共享的内存地址空间
//
// 例如：VGA 显存映射到 0x40000-0xBFFFF
//   mov [0x40000], 0xFF0000  // 写入红色像素（普通的内存写入指令）
//
// NEMU 的实现：
//   1. vaddr_read/write → paddr_read/write
//   2. paddr_read/write 检查 is_mmio(addr)
//   3. 如果是 MMIO，调用 mmio_read/write 而不是访问 pmem[]
//   4. mmio_read/write 访问 mmio_space_pool 并触发回调
//
// ──────────────────────────────────────────────────────────────────────────────
// MMIO 映射表示例：
//
// 假设有 3 个设备：
//   VGA 显存：0x40000-0xBFFFF（512KB）
//   VGA 控制：0xA0000-0xA0FFF（4KB）
//   网卡：0xFEBF0000-0xFEBFFFFF（64KB）
//
// maps[] 数组：
//   [0] = { low: 0x40000, high: 0xBFFFF, space: &pool[0], callback: vga_vmem_handler }
//   [1] = { low: 0xA0000, high: 0xA0FFF, space: &pool[524288], callback: vga_ctrl_handler }
//   [2] = { low: 0xFEBF0000, high: 0xFEBFFFFF, space: &pool[528384], callback: net_handler }
//
// 当客户程序执行：mov [0x40100], eax
//   → vaddr_write(0x40100, 4, eax)
//   → paddr_write(0x40100, 4, eax)
//   → is_mmio(0x40100) 返回 0（第一个映射）
//   → mmio_write(0x40100, 4, eax, 0)
//   → 写入 pool[0x100]（相对于映射起始地址的偏移）
//   → 调用 vga_vmem_handler(0x40100, 4, true)
//
// ──────────────────────────────────────────────────────────────────────────────
// 回调函数的用途：
//
// 回调函数让设备知道有人访问了它的寄存器，可以执行相应的动作：
//
// 1. VGA 显存回调：
//    void vga_vmem_handler(paddr_t addr, int len, bool is_write) {
//      if (is_write) screen_dirty = true;  // 标记屏幕需要刷新
//    }
//
// 2. 网卡发送寄存器回调：
//    void net_tx_handler(paddr_t addr, int len, bool is_write) {
//      if (is_write) send_packet();  // 触发数据包发送
//    }
//
// 3. DMA 控制器回调：
//    void dma_ctrl_handler(paddr_t addr, int len, bool is_write) {
//      if (is_write) start_dma_transfer();  // 启动 DMA 传输
//    }
//
// ──────────────────────────────────────────────────────────────────────────────
// MMIO vs 端口 I/O 对比：
//
// 特性           | 端口 I/O (PIO)        | MMIO
// ───────────────┼───────────────────────┼──────────────────────────────
// 指令           | in/out（专用指令）    | mov/add/sub（普通内存指令）
// 地址空间       | 独立（0-65535）       | 共享（与内存同一地址空间）
// 访问速度       | 慢（旧硬件）          | 快（现代硬件）
// 权限控制       | 需要 IOPL 权限        | 通过分页保护（页表）
// 缓存           | 不可缓存              | 需要标记为不可缓存（页表属性）
// DMA 支持       | 需要额外配置          | 天然支持（设备直接访问内存）
// 典型应用       | 传统 ISA/PCI 设备     | 现代 PCIe 设备
//
// x86 架构同时支持两种方式，但现代设备倾向于使用 MMIO。
//
// ──────────────────────────────────────────────────────────────────────────────
// 为什么 MMIO 需要一个内存池？
//
// 问题：设备寄存器不是真正的 RAM，为什么需要 mmio_space_pool？
//
// 答案：
//   1. 统一接口：设备可以像操作普通内存一样读写 mmio_space
//   2. 缓冲区：某些设备需要缓冲区（例如 VGA 显存、网卡收发缓冲区）
//   3. 简化实现：避免为每个设备单独管理内存
//
// 例如：VGA 显存
//   - 真实硬件：显卡有独立的显存芯片
//   - NEMU 模拟：用 mmio_space_pool 的一块区域模拟显存
//   - 客户程序写显存 → 写入 mmio_space_pool → update_screen 读取并渲染
//
// ──────────────────────────────────────────────────────────────────────────────
// 常见的 MMIO 地址范围（x86 架构）：
//
// 地址范围           | 用途
// ───────────────────┼──────────────────────────────────────────────
// 0x000A0000-0x000BFFFF | VGA 显存（128KB）
// 0x000C0000-0x000FFFFF | BIOS ROM（256KB）
// 0xFEC00000-0xFECFFFFF | I/O APIC（高级中断控制器）
// 0xFEE00000-0xFEEFFFFF | Local APIC（本地中断控制器）
// 0xFED00000-0xFEDFFFFF | HPET（高精度定时器）
// 0xF0000000-0xFFFFFFFF | PCI 配置空间、设备 MMIO
//
// 这些地址不对应 RAM，系统内存（RAM）通常占用：
//   0x00000000-0x9FFFF（前 640KB）
//   0x00100000-...（1MB 以上）
//
// ──────────────────────────────────────────────────────────────────────────────
