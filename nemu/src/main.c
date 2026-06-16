// main.c - NEMU 程序入口
// 功能：NEMU 的 main 函数，程序的起点
// 工作流程：初始化 → 进入主循环

int init_monitor(int, char *[]);
// 声明：初始化监视器函数（在 monitor.c 中定义）
void ui_mainloop(int);
// 声明：用户界面主循环函数（在 ui.c 中定义）

int main(int argc, char *argv[]) {
  /* Initialize the monitor. */
  // 第一步：初始化监视器
  // 包括：解析命令行参数、加载客户程序、初始化 CPU、初始化设备等
  int is_batch_mode = init_monitor(argc, argv);
  // 返回值：是否为批处理模式（-b 参数）

  /* Receive commands from user. */
  // 第二步：进入主循环
  // 批处理模式：直接运行客户程序到结束
  // 交互模式：显示 "(nemu) " 提示符，等待用户输入命令（si、c、info、x 等）
  ui_mainloop(is_batch_mode);

  return 0;                             // 程序正常退出
}

// ──────────────────────────────────────────────────────────────────────────────
// NEMU 完整启动流程：
//
// 1. main()
//    ↓
// 2. init_monitor()（在 monitor.c）
//    - parse_args()：解析 -b、-l 等参数
//    - init_log()：打开日志文件
//    - reg_test()：测试寄存器结构体
//    - init_difftest()：初始化差分测试（可选）
//    - load_img()：加载客户程序到内存
//    - restart()：设置 eip、eflags 初始值
//    - init_regex()：编译正则表达式
//    - init_wp_pool()：初始化监视点池
//    - init_device()：初始化所有设备
//    - welcome()：显示欢迎信息
//    ↓
// 3. ui_mainloop()（在 ui.c）
//    if (批处理模式):
//      cpu_exec(-1)  // 运行到程序结束
//      return
//    else:
//      while (1):
//        显示 "(nemu) " 提示符
//        读取用户命令
//        执行命令（si、c、info r、x、p 等）
//
// ──────────────────────────────────────────────────────────────────────────────
// 运行模式对比：
//
// 交互模式（默认）：
//   $ ./nemu program.bin
//   Welcome to NEMU!
//   (nemu) c
//   nemu: HIT GOOD TRAP at eip = 0x00100026
//   (nemu) q
//
// 批处理模式（-b）：
//   $ ./nemu -b program.bin
//   Welcome to NEMU!
//   nemu: HIT GOOD TRAP at eip = 0x00100026
//   $  （直接退出，回到 shell）
//
// 用途：
//   - 交互模式：手动调试，单步执行，查看寄存器
//   - 批处理模式：自动化测试，脚本化运行
//
// ──────────────────────────────────────────────────────────────────────────────
