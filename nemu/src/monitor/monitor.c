// monitor.c - NEMU 监视器初始化（PA1/PA2 核心文件）
// 功能：NEMU 的启动和初始化流程
// 包括：加载客户程序、初始化 CPU、初始化调试器、初始化设备等

#include "nemu.h"         // NEMU 全局定义
#include <unistd.h>       // getopt 函数（命令行参数解析）

#define ENTRY_START 0x100000
// 客户程序的入口地址：0x100000（1MB）
// 为什么是 1MB：
//   - 0x00000000 - 0x000FFFFF（前 1MB）：预留给 BIOS、设备内存映射等
//   - 0x00100000 开始：可用的物理内存
// 这和真实的 x86 系统布局一致（历史原因：IBM PC 的内存布局）

void init_difftest();     // 初始化差分测试（与 QEMU 对比）
void init_regex();        // 初始化正则表达式引擎（用于表达式求值）
void init_wp_pool();      // 初始化监视点池（watchpoint pool）
void init_device();       // 初始化设备（定时器、VGA、键盘等）

void reg_test();          // 测试寄存器结构体实现是否正确
void init_qemu_reg();     // 初始化 QEMU 的寄存器（差分测试）
bool gdb_memcpy_to_qemu(uint32_t, void *, int);
// 复制内存到 QEMU（差分测试）

FILE *log_fp = NULL;      // 日志文件指针（用于 Log 宏）
static char *log_file = NULL;     // 日志文件路径（-l 参数指定）
static char *img_file = NULL;     // 客户程序镜像文件路径（命令行第一个参数）
static int is_batch_mode = false; // 是否为批处理模式（-b 参数）
                                  // 批处理模式：不显示交互式提示符，运行完就退出

// init_log: 打开日志文件
// 功能：如果指定了 -l 参数，就打开日志文件用于记录 Log 输出
static inline void init_log() {
#ifdef DEBUG
  if (log_file == NULL) return;   // 没有指定日志文件，直接返回
  log_fp = fopen(log_file, "w");  // 以写模式打开日志文件
  Assert(log_fp, "Can not open '%s'", log_file);
  // 断言：文件必须能打开，否则报错退出
#endif
}

// welcome: 显示欢迎信息
// 功能：打印 NEMU 的欢迎信息和编译时间
static inline void welcome() {
  printf("Welcome to NEMU!\n");
  Log("Build time: %s, %s", __TIME__, __DATE__);
  // __TIME__ 和 __DATE__ 是编译器预定义宏，记录编译时间
  // 用途：判断 NEMU 的版本（如果有 bug，看看是不是旧版本）
  printf("For help, type \"help\"\n");
}

// load_default_img: 加载内置的默认程序
// 功能：如果没有指定客户程序文件，就加载一个简单的测试程序
// 返回：程序的字节数
static inline int load_default_img() {
  const uint8_t img []  = {
    // 这是一个手写的机器码程序（十六进制字节）
    // 下面是对应的汇编代码（由注释给出）
    0xb8, 0x34, 0x12, 0x00, 0x00,        // 100000:  movl  $0x1234,%eax
    // b8 = mov eax, imm32 的操作码
    // 34 12 00 00 = 0x00001234（小端序）
    // 语义：eax = 0x1234

    0xb9, 0x27, 0x00, 0x10, 0x00,        // 100005:  movl  $0x100027,%ecx
    // 语义：ecx = 0x100027

    0x89, 0x01,                          // 10000a:  movl  %eax,(%ecx)
    // 89 01 = mov [ecx], eax
    // 语义：[0x100027] = eax = 0x1234

    0x66, 0xc7, 0x41, 0x04, 0x01, 0x00,  // 10000c:  movw  $0x1,0x4(%ecx)
    // 66 = 操作数大小前缀（16位）
    // c7 = mov [rm], imm
    // 41 04 = ModR/M + disp8: [ecx+4]
    // 语义：[0x10002b] = 0x0001（2 字节）

    0xbb, 0x02, 0x00, 0x00, 0x00,        // 100012:  movl  $0x2,%ebx
    // 语义：ebx = 2

    0x66, 0xc7, 0x84, 0x99, 0x00, 0xe0,  // 100017:  movw  $0x1,-0x2000(%ecx,%ebx,4)
    0xff, 0xff, 0x01, 0x00,
    // 84 99 = ModR/M + SIB: [ecx + ebx*4 + disp32]
    // 00 e0 ff ff = -0x2000（小端序，有符号）
    // 语义：[ecx + ebx*4 - 0x2000] = [0x100027 + 2*4 - 0x2000] = [0xfe02f] = 0x0001

    0xb8, 0x00, 0x00, 0x00, 0x00,        // 100021:  movl  $0x0,%eax
    // 语义：eax = 0（正常退出码）

    0xd6,                                // 100026:  nemu_trap
    // d6 = NEMU 特殊指令：根据 eax 的值判断程序是否正常结束
    // eax=0 → GOOD TRAP（测试通过）
  };

  Log("No image is given. Use the default build-in image.");
  // 提示：没有指定镜像文件，使用默认的内置程序

  memcpy(guest_to_host(ENTRY_START), img, sizeof(img));
  // 把 img 数组复制到客户机内存的 0x100000 地址
  // guest_to_host(ENTRY_START) = &pmem[0x100000]

  return sizeof(img);                   // 返回程序大小（字节数）
}

// load_img: 加载客户程序镜像到内存
// 功能：如果指定了镜像文件，就从文件读取；否则使用默认程序
static inline void load_img() {
  long size;                            // 程序大小（字节数）
  if (img_file == NULL) {               // 没有指定镜像文件
    size = load_default_img();          // 加载默认程序
  }
  else {                                // 指定了镜像文件
    int ret;

    FILE *fp = fopen(img_file, "rb");   // 以二进制只读模式打开文件
    Assert(fp, "Can not open '%s'", img_file);

    Log("The image is %s", img_file);   // 记录镜像文件路径

    fseek(fp, 0, SEEK_END);             // 移动到文件末尾
    size = ftell(fp);                   // 获取文件大小（字节数）

    fseek(fp, 0, SEEK_SET);             // 移动回文件开头
    ret = fread(guest_to_host(ENTRY_START), size, 1, fp);
    // 从文件读取 size 字节到客户机内存的 0x100000 地址
    // fread(buf, size, count, fp) 读取 count 个 size 字节的块
    assert(ret == 1);                   // 断言：必须成功读取 1 个块

    fclose(fp);                         // 关闭文件
  }

#ifdef DIFF_TEST
  gdb_memcpy_to_qemu(ENTRY_START, guest_to_host(ENTRY_START), size);
  // 差分测试：把同样的程序也加载到 QEMU 的内存中
  // 这样 NEMU 和 QEMU 就从相同的程序开始执行
#endif
}

// restart: 重启虚拟计算机
// 功能：初始化 CPU 寄存器，设置初始状态
static inline void restart() {
  /* Set the initial instruction pointer. */
  cpu.eip = ENTRY_START;                // 设置 eip 为程序入口地址（0x100000）
                                        // CPU 会从这个地址开始取指令执行
  cpu.eflags.val = 0x2;                 // 初始化 EFLAGS 寄存器
                                        // 0x2 = bit 1 = Reserved（保留位，永远为 1）
                                        // 其他标志位都清零（CF=0, ZF=0, SF=0, OF=0, IF=0 等）

#ifdef DIFF_TEST
  init_qemu_reg();                      // 差分测试：把 QEMU 的寄存器也设置成相同的值
#endif
}

// parse_args: 解析命令行参数
// 功能：处理 NEMU 的命令行选项
// 参数：argc = 参数个数，argv = 参数字符串数组
static inline void parse_args(int argc, char *argv[]) {
  int o;
  while ( (o = getopt(argc, argv, "-bl:")) != -1) {
    // getopt 逐个解析命令行参数
    // "-bl:" 表示支持的选项：
    //   -b：无参数选项（批处理模式）
    //   -l：有参数选项（指定日志文件）
    //   -：允许非选项参数（镜像文件路径）
    switch (o) {
      case 'b': is_batch_mode = true; break;
      // -b：批处理模式
      // 效果：运行完程序就退出，不显示 "(nemu) " 提示符
      // 用途：自动化测试脚本

      case 'l': log_file = optarg; break;
      // -l log_file：指定日志文件
      // optarg = 选项的参数（例如 -l abc.txt，optarg = "abc.txt"）
      // 效果：所有 Log() 输出都写入文件，而不是 stdout

      case 1:
                if (img_file != NULL) Log("too much argument '%s', ignored", optarg);
                // 如果已经有镜像文件了，忽略后续的参数
                else img_file = optarg;
                // 否则把这个参数当作镜像文件路径
                break;

      default:
                panic("Usage: %s [-b] [-l log_file] [img_file]", argv[0]);
                // 未知选项：打印用法并退出
    }
  }
}

// init_monitor: 初始化 NEMU 监视器（主初始化函数）
// 功能：执行所有的全局初始化，准备运行客户程序
// 参数：argc, argv = 命令行参数
// 返回：是否为批处理模式（1=是，0=否）
int init_monitor(int argc, char *argv[]) {
  /* Perform some global initialization. */
  // 执行全局初始化（按顺序）

  /* Parse arguments. */
  parse_args(argc, argv);               // 1. 解析命令行参数

  /* Open the log file. */
  init_log();                           // 2. 打开日志文件

  /* Test the implementation of the `CPU_state' structure. */
  reg_test();                           // 3. 测试寄存器结构体
                                        // 检查 union 的对齐、位域的正确性等

#ifdef DIFF_TEST
  /* Fork a child process to perform differential testing. */
  init_difftest();                      // 4. 初始化差分测试（fork QEMU 子进程）
#endif

  /* Load the image to memory. */
  load_img();                           // 5. 加载客户程序到内存

  /* Initialize this virtual computer system. */
  restart();                            // 6. 初始化 CPU 状态（设置 eip、eflags）

  /* Compile the regular expressions. */
  init_regex();                         // 7. 编译正则表达式（用于表达式求值）

  /* Initialize the watchpoint pool. */
  init_wp_pool();                       // 8. 初始化监视点池

  /* Initialize devices. */
  init_device();                        // 9. 初始化设备（定时器、VGA、键盘等）

  /* Display welcome message. */
  welcome();                            // 10. 显示欢迎信息

  return is_batch_mode;                 // 返回是否为批处理模式
}

// ──────────────────────────────────────────────────────────────────────────────
// NEMU 启动流程总结：
//
// 1. main() 函数（在 main.c）：
//    int is_batch_mode = init_monitor(argc, argv);  ← 调用 init_monitor
//    ui_mainloop(is_batch_mode);                    ← 进入主循环
//
// 2. init_monitor() 执行 10 个初始化步骤
//
// 3. ui_mainloop()：
//    if (is_batch_mode) {
//      cpu_exec(-1);  // 批处理模式：一直运行到程序结束
//    } else {
//      while (1) {
//        显示提示符 "(nemu) "
//        读取用户命令
//        执行命令（si、c、info、x 等）
//      }
//    }
//
// ──────────────────────────────────────────────────────────────────────────────
// NEMU 命令行使用示例：
//
// 1. 运行默认程序（交互模式）：
//    $ ./nemu
//    Welcome to NEMU!
//    (nemu) c
//    nemu: HIT GOOD TRAP at eip = 0x00100026
//
// 2. 运行指定程序：
//    $ ./nemu path/to/program.bin
//
// 3. 批处理模式（自动运行）：
//    $ ./nemu -b program.bin
//    （运行完直接退出，不显示提示符）
//
// 4. 记录日志：
//    $ ./nemu -l nemu.log program.bin
//    （所有 Log() 输出都写入 nemu.log）
//
// 5. 组合使用：
//    $ ./nemu -b -l test.log test.bin
//    （批处理 + 日志）
//
// ──────────────────────────────────────────────────────────────────────────────
// 客户程序镜像的制作：
//
// 1. 编写汇编或 C 代码：
//    // test.c
//    #define nemu_trap(code) asm volatile(".byte 0xd6" : :"a"(code))
//    void _start() {
//      int a = 1 + 2;
//      nemu_trap(a == 3 ? 0 : 1);  // 0=成功，1=失败
//    }
//
// 2. 编译成可执行文件：
//    $ gcc -m32 -march=i386 -nostdlib -Ttext=0x100000 test.c -o test.elf
//    -m32：生成 32 位代码
//    -march=i386：使用基本的 i386 指令集
//    -nostdlib：不链接标准库
//    -Ttext=0x100000：代码段起始地址为 0x100000
//
// 3. 提取纯二进制代码：
//    $ objcopy -O binary -j .text test.elf test.bin
//    只保留 .text 段（代码段），去掉 ELF 头和其他段
//
// 4. 运行：
//    $ ./nemu test.bin
//
// ──────────────────────────────────────────────────────────────────────────────
