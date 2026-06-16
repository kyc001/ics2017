// memory.h - 内存访问接口（PA2/PA4 核心头文件）
// 功能：提供虚拟地址、物理地址的读写接口，实现地址转换和分页机制
// PA2：实现物理地址访问（paddr_read/paddr_write）
// PA4：实现虚拟地址访问和分页（vaddr_read/vaddr_write，page_translate）

#ifndef __MEMORY_H__      // 头文件保护
#define __MEMORY_H__

#include "common.h"       // 公共类型定义
#include "device/mmio.h"  // MMIO（内存映射 I/O）接口
#include "cpu/reg.h"      // CPU 寄存器定义（需要访问 cr0、cr3）
#include "memory/mmu.h"   // MMU（内存管理单元）相关定义（页表项结构等）

#define PMEM_SIZE (128 * 1024 * 1024)
// 物理内存大小：128MB = 128 * 1024 * 1024 字节 = 0x08000000 字节
// 这是 NEMU 模拟的计算机拥有的物理内存总量
// 客户机程序看到的物理地址范围：0x00000000 - 0x07FFFFFF

extern uint8_t pmem[];    // 物理内存数组（定义在 memory.c）
                          // pmem[i] = 物理地址 i 处的字节

/* convert the guest physical address in the guest program to host virtual address in NEMU */
// 地址转换宏：客户机物理地址 → NEMU 宿主机虚拟地址
// 例如：客户机物理地址 0x100000 → NEMU 的 &pmem[0x100000]
#define guest_to_host(p) ((void *)(pmem + (unsigned)p))

/* convert the host virtual address in NEMU to guest physical address in the guest program */
// 地址转换宏：NEMU 宿主机虚拟地址 → 客户机物理地址
// 例如：NEMU 的 &pmem[0x100000] → 客户机物理地址 0x100000
#define host_to_guest(p) ((paddr_t)((void *)p - (void *)pmem))

// paddr_read: 读取物理地址的数据（PA2 核心函数）
// 参数：addr = 物理地址，len = 读取长度（1/2/4 字节）
// 返回：读取的数据（小端序）
// 功能：① 检查是否为 MMIO（内存映射 I/O）地址
//       ② 检查地址是否越界
//       ③ 从 pmem[] 数组读取数据
static inline uint32_t paddr_read(paddr_t addr, int len) {
  int map_NO = is_mmio(addr);           // 检查是否为 MMIO 地址（设备寄存器）
                                        // MMIO：把设备寄存器映射到内存地址空间
                                        // 例如：显存、串口寄存器等
  if (map_NO != -1) {                   // 如果是 MMIO 地址
    return mmio_read(addr, len, map_NO);// 转交给设备处理（不是读真正的内存）
  }

  Assert(addr + len - 1 < PMEM_SIZE,
      "physical address(0x%08x) is out of bound", addr);
  // 断言：物理地址必须在有效范围内（0 - 128MB）
  // 如果越界就报错停止（防止访问非法内存）

  uint8_t *p = (uint8_t *)guest_to_host(addr);
  // p = pmem + addr（把客户机物理地址转换成 NEMU 宿主机指针）

  assert(len >= 1 && len <= 4);         // 长度必须是 1、2、3 或 4 字节
  uint32_t ret = 0;
  for (int i = 0; i < len; i ++) {      // 按小端序拼接字节
    ret |= (uint32_t)p[i] << (i * 8);   // 低地址字节放在低位
  }
  // 例如：读取地址 0x100（假设内容为 0x12 0x34 0x56 0x78）
  //   len=1: ret = 0x12
  //   len=2: ret = 0x3412（小端序：低字节 0x12 在低位）
  //   len=4: ret = 0x78563412
  return ret;
}

// paddr_write: 写入物理地址（PA2 核心函数）
// 参数：addr = 物理地址，len = 写入长度，data = 要写入的数据
// 功能：① 检查是否为 MMIO 地址
//       ② 检查地址是否越界
//       ③ 向 pmem[] 数组写入数据
static inline void paddr_write(paddr_t addr, int len, uint32_t data) {
  int map_NO = is_mmio(addr);           // 检查是否为 MMIO 地址
  if (map_NO != -1) {                   // 如果是 MMIO 地址
    mmio_write(addr, len, data, map_NO);// 转交给设备处理
    return;
  }

  Assert(addr + len - 1 < PMEM_SIZE,
      "physical address(0x%08x) is out of bound", addr);
  // 断言：地址必须在有效范围内

  uint8_t *p = (uint8_t *)guest_to_host(addr);
  assert(len >= 1 && len <= 4);
  for (int i = 0; i < len; i ++) {      // 按小端序拆分字节
    p[i] = data >> (i * 8);             // 低位字节写到低地址
  }
  // 例如：写入地址 0x100，data = 0x78563412，len = 4
  //   pmem[0x100] = 0x12
  //   pmem[0x101] = 0x34
  //   pmem[0x102] = 0x56
  //   pmem[0x103] = 0x78
}

// page_translate: 虚拟地址 → 物理地址转换（PA4 分页机制的核心）
// 参数：addr = 虚拟地址，write = 是否为写操作
// 返回：对应的物理地址
// 功能：通过两级页表（页目录 + 页表）完成地址转换
// 工作原理：
//   虚拟地址 32 位分解：
//     [31:22] = 页目录索引（10 位，0-1023）
//     [21:12] = 页表索引（10 位，0-1023）
//     [11:0]  = 页内偏移（12 位，0-4095）
//   转换步骤：
//     ① 从 CR3 寄存器获取页目录基址
//     ② 用页目录索引查页目录，得到页表基址
//     ③ 用页表索引查页表，得到物理页帧号
//     ④ 物理地址 = 物理页帧号 << 12 | 页内偏移
static inline paddr_t page_translate(vaddr_t addr, bool write) {
  paddr_t pdir_base = cpu.cr3 & ~PAGE_MASK;
  // 页目录基址 = CR3 寄存器的高 20 位（低 12 位清零）
  // CR3 寄存器存放页目录的物理地址

  paddr_t pde_addr = pdir_base + ((addr >> 22) & 0x3ff) * 4;
  // 页目录项（PDE）地址 = 页目录基址 + 页目录索引 * 4
  // (addr >> 22) & 0x3ff = 虚拟地址的高 10 位（页目录索引）
  // 每个页目录项 4 字节，所以 * 4

  PDE pde;                              // 页目录项结构体
  pde.val = paddr_read(pde_addr, 4);    // 从内存读取页目录项
  Assert(pde.present, "page directory entry not present: eip = 0x%08x, vaddr = 0x%08x", cpu.eip, addr);
  // 断言：页目录项的 present 位必须为 1（页表存在）
  // 如果为 0，说明这个页表还没有分配，触发缺页异常

  pde.accessed = 1;                     // 设置 accessed 位（表示这个页目录项被访问过）
  paddr_write(pde_addr, 4, pde.val);    // 把更新后的页目录项写回内存

  paddr_t ptab_base = pde.page_frame << 12;
  // 页表基址 = 页目录项的页帧号 << 12（左移 12 位相当于乘以 4096）
  // 页帧号是 20 位，表示 4KB 对齐的物理地址

  paddr_t pte_addr = ptab_base + ((addr >> 12) & 0x3ff) * 4;
  // 页表项（PTE）地址 = 页表基址 + 页表索引 * 4
  // (addr >> 12) & 0x3ff = 虚拟地址的中间 10 位（页表索引）

  PTE pte;                              // 页表项结构体
  pte.val = paddr_read(pte_addr, 4);    // 从内存读取页表项
  Assert(pte.present, "page table entry not present: eip = 0x%08x, vaddr = 0x%08x", cpu.eip, addr);
  // 断言：页表项的 present 位必须为 1（物理页存在）
  // 如果为 0，触发缺页异常（操作系统会从磁盘加载页面）

  pte.accessed = 1;                     // 设置 accessed 位
  if (write) {                          // 如果是写操作
    pte.dirty = 1;                      // 设置 dirty 位（表示这个页被修改过）
  }
  paddr_write(pte_addr, 4, pte.val);    // 把更新后的页表项写回内存

  return (pte.page_frame << 12) | (addr & PAGE_MASK);
  // 物理地址 = 页表项的页帧号 << 12 | 虚拟地址的低 12 位（页内偏移）
  // PAGE_MASK = 0xFFF（低 12 位全 1）
  // 例如：虚拟地址 0x12345678 → 物理地址 0xABCDE678
  //       （页帧号 0xABCDE，页内偏移 0x678）
}

// paging_enabled: 检查分页机制是否开启
// 返回：true = 分页开启，false = 分页关闭
// 判断方法：检查 CR0 寄存器的 PG 位（bit 31）
static inline bool paging_enabled(void) {
  CR0 cr0;                              // CR0 寄存器结构体
  cr0.val = cpu.cr0;                    // 读取 CR0 寄存器的值
  return cr0.paging;                    // 返回 PG 位（bit 31）
}

// vaddr_read: 读取虚拟地址的数据（PA4 核心函数）
// 参数：addr = 虚拟地址，len = 读取长度
// 返回：读取的数据
// 功能：① 如果分页关闭，虚拟地址 = 物理地址，直接调用 paddr_read
//       ② 如果分页开启，先通过 page_translate 转换成物理地址，再读取
//       ③ 特殊处理：如果读取跨越页边界，分成两次读取
static inline uint32_t vaddr_read(vaddr_t addr, int len) {
  if (!paging_enabled()) {              // 如果分页未开启
    return paddr_read(addr, len);       // 虚拟地址 = 物理地址，直接读取
  }
  if ((addr & PAGE_MASK) + len > PAGE_SIZE) {
    // 特殊情况：读取跨越页边界
    // 例如：addr = 0x1FFE，len = 4
    //       0x1FFE 和 0x1FFF 在第 1 页，0x2000 和 0x2001 在第 2 页
    //       这两个页可能映射到不连续的物理地址，必须分两次读取
    uint32_t lo_len = PAGE_SIZE - (addr & PAGE_MASK);
    // 第 1 页剩余字节数 = 4096 - (addr % 4096)
    uint32_t lo = vaddr_read(addr, lo_len);
    // 读取第 1 页的剩余字节
    uint32_t hi = vaddr_read(addr + lo_len, len - lo_len);
    // 读取第 2 页的开头字节
    return lo | (hi << (lo_len * 8));   // 拼接两次读取的结果（小端序）
  }
  return paddr_read(page_translate(addr, false), len);
  // 正常情况：先把虚拟地址转换成物理地址，再读取
  // false 表示这是读操作（不设置 dirty 位）
}

// vaddr_write: 写入虚拟地址（PA4 核心函数）
// 参数：addr = 虚拟地址，len = 写入长度，data = 要写入的数据
// 功能：类似 vaddr_read，但要处理跨页写入
static inline void vaddr_write(vaddr_t addr, int len, uint32_t data) {
  if (!paging_enabled()) {              // 如果分页未开启
    paddr_write(addr, len, data);       // 虚拟地址 = 物理地址，直接写入
    return;
  }
  if ((addr & PAGE_MASK) + len > PAGE_SIZE) {
    // 特殊情况：写入跨越页边界，分成两次写入
    uint32_t lo_len = PAGE_SIZE - (addr & PAGE_MASK);
    uint32_t lo_mask = (1ull << (lo_len * 8)) - 1;
    // 低位掩码：例如 lo_len=2 → lo_mask=0xFFFF
    vaddr_write(addr, lo_len, data & lo_mask);
    // 写入第 1 页的剩余字节（data 的低位）
    vaddr_write(addr + lo_len, len - lo_len, data >> (lo_len * 8));
    // 写入第 2 页的开头字节（data 的高位）
    return;
  }
  paddr_write(page_translate(addr, true), len, data);
  // 正常情况：先把虚拟地址转换成物理地址，再写入
  // true 表示这是写操作（设置 dirty 位）
}

#endif      // __MEMORY_H__ 结束

// ──────────────────────────────────────────────────────────────────────────────
// 内存访问层次总结：
//
// PA2（无分页）：
//   指令使用地址 → vaddr_read/vaddr_write → paddr_read/paddr_write → pmem[]
//   （虚拟地址 = 物理地址）
//
// PA4（开启分页）：
//   指令使用地址 → vaddr_read/vaddr_write → page_translate（查页表）→
//   → paddr_read/paddr_write → pmem[]
//   （虚拟地址通过页表转换成物理地址）
//
// 分页的好处：
//   ① 每个进程有独立的地址空间（看到的都是 0 开始的地址）
//   ② 物理内存可以不连续（虚拟连续 → 物理分散）
//   ③ 可以实现虚拟内存（物理内存不够时，把页换出到磁盘）
//   ④ 内存保护（不同进程的页表隔离，防止互相访问）
// ──────────────────────────────────────────────────────────────────────────────
