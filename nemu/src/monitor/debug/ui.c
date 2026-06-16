// ui.c - NEMU 调试器的命令行界面（User Interface）
// 功能：接收用户输入的调试命令（si/info/x/p/w/d 等），解析并分发到对应的处理函数
// 这就像 gdb 调试器的命令行界面，让你能一步步控制程序执行、查看状态

#include "monitor/monitor.h"      // NEMU 监视器（调试器）相关声明
#include "monitor/expr.h"         // 表达式求值函数 expr() 的声明（p/x 命令需要用它）
#include "monitor/watchpoint.h"   // 监视点相关函数声明（w/d/info w 命令需要用它）
#include "nemu.h"                 // NEMU 全局定义（cpu 状态、vaddr_read 读内存等）

#include <stdlib.h>               // 标准库函数（free 等）
#include <readline/readline.h>    // readline 库：读入一行命令，支持行内编辑（左右方向键移动光标）
#include <readline/history.h>     // readline 历史记录功能：支持上下方向键翻历史命令

void cpu_exec(uint64_t);   // 声明：执行若干条指令（函数体在别处实现）
                           // 参数是要执行的指令条数，例如 cpu_exec(5) 表示执行 5 条指令

/* We use the `readline' library to provide more flexibility to read from stdin. */
// 读一行用户输入的命令，返回这一行字符串
// 使用 readline 库而不是简单的 scanf/fgets，因为 readline 支持：
//   - 行内编辑（左右方向键移动光标、Backspace 删除）
//   - 历史记录（上下方向键翻阅之前输入过的命令）
char* rl_gets() {
  static char *line_read = NULL;          // static 变量：跨多次函数调用都保留，用来缓存上一行的内存指针
                                          // 这样每次调用时可以先释放上次申请的内存，避免内存泄漏

  if (line_read) {                        // 如果上次的指针不为空（上次读过一行）
    free(line_read);                      // 先释放上次那一行的内存
    line_read = NULL;                     // 指针置空，防止悬空指针
  }

  line_read = readline("(nemu) ");        // 打印提示符 "(nemu) " 并等待用户输入一行
                                          // readline 会返回用户输入的字符串（动态分配的内存）

  if (line_read && *line_read) {          // 如果这一行非空（不是只按了回车）
    add_history(line_read);               // 把它加入历史记录，这样用户下次可以用上下方向键找回来
  }

  return line_read;                       // 返回这一行字符串给调用者
}

// cmd_c: 命令 "c" (continue) 的处理函数 - 连续运行程序直到结束或遇到断点
static int cmd_c(char *args) {
  cpu_exec(-1);     // 执行 -1 条指令？其实 -1 会被当作 uint64_t 类型，变成最大值（约 1.8×10^19）
                    // 相当于"一直执行下去"，直到程序结束或触发断点/监视点
  return 0;         // 返回 0 表示继续保持在调试器主循环中（不退出）
}

// cmd_q: 命令 "q" (quit) 的处理函数 - 退出 NEMU
static int cmd_q(char *args) {
  return -1;        // 返回负数，主循环看到后会退出（这是唯一会返回负数的命令）
}

static int cmd_help(char *args);    // 先声明 cmd_help（函数体在后面）
                                    // 因为下面的 cmd_table 数组要引用它，必须先声明

static int cmd_si(char *args);      // 先声明（实际定义紧跟着，这种写法有点冗余但不影响）
// cmd_si: 命令 "si [N]" (step instruction) 的处理函数 - 单步执行 N 条指令
// 用法：si      执行 1 条指令（默认）
//       si 5    执行 5 条指令
static int cmd_si(char *args) {
  int n = 1;                              // 默认执行 1 条指令
  if (args != NULL) {                     // 如果用户给了参数（例如 "si 5" 中的 "5"）
    if (sscanf(args, "%d", &n) != 1 || n <= 0) {
      // sscanf 解析失败（不是合法数字）或者 n≤0（非正数）
      printf("Usage: si [N]\n");          // 提示正确用法
      return 0;                           // 不执行，返回 0 继续等待下一条命令
    }
  }

  Log("cmd_si: n=%d, eip_before=0x%08x", n, cpu.eip);  // 调试输出：记录执行前的 eip（程序计数器）
  cpu_exec(n);                            // 真正执行 n 条指令
  Log("cmd_si: eip_after=0x%08x", cpu.eip);            // 调试输出：记录执行后的 eip
  return 0;
}

static int cmd_info(char *args);    // 先声明
// cmd_info: 命令 "info r" 和 "info w" 的处理函数 - 查看程序状态
// 用法：info r    查看寄存器（打印 8 个通用寄存器 + eip 的值）
//       info w    查看监视点（打印当前所有监视点的信息）
static int cmd_info(char *args) {
  int i;
  char *subcmd;

  if (args == NULL) {                     // 用户只输入 "info" 没有给子命令
    printf("Usage: info r\n");            // 提示需要子命令
    return 0;
  }

  subcmd = strtok(args, " ");             // 取第一个子命令词（"r" 或 "w"）
                                          // strtok 会把字符串按空格分割，第一次调用取第一个 token
  Log("cmd_info: subcmd=%s", subcmd);     // 调试输出：记录子命令是什么

  if (strcmp(subcmd, "r") == 0) {         // 子命令是 "r" → 查看寄存器
    for (i = 0; i < 8; i++) {             // 遍历 8 个通用寄存器
      printf("%-3s\t0x%08x\t%u\n", regsl[i], reg_l(i), reg_l(i));
      // regsl[i] = 寄存器名字（例如 "eax"）
      // reg_l(i) = 第 i 个寄存器的 32 位值
      // 同时按十六进制和十进制两种格式打印，方便观察
      // %-3s 表示左对齐，占 3 个字符宽度
    }
    printf("eip\t0x%08x\t%u\n", cpu.eip, cpu.eip);  // 最后打印 eip（程序计数器）
    return 0;
  }

  if (strcmp(subcmd, "w") == 0) {         // 子命令是 "w" → 查看监视点
    info_watchpoints();                   // 转交给监视点模块的函数去打印
    return 0;
  }

  printf("Unknown info subcommand '%s'\n", subcmd);  // 既不是 r 也不是 w → 未知子命令
  return 0;
}

static int cmd_x(char *args);       // 先声明
// cmd_x: 命令 "x N EXPR" (examine memory) 的处理函数 - 扫描内存
// 用法：x 10 0x100000     从地址 0x100000 开始，连续打印 10 个 4 字节的内存内容
//       x 5 $esp          从 esp 寄存器指向的地址开始，打印 5 个 4 字节
//       x 3 $eip+8        从 eip+8 的地址开始，打印 3 个 4 字节
static int cmd_x(char *args) {
  int n, i;
  char *n_str;                            // 指向个数 N 那一段字符串
  char *expr_str;                         // 指向表达式那一段字符串
  bool success = true;                    // 标记表达式求值是否成功
  uint32_t addr;                          // 存放表达式算出的起始地址

  if (args == NULL) {                     // 用户没给参数
    printf("Usage: x N EXPR\n");          // 提示正确用法
    return 0;
  }

  n_str = strtok(args, " ");              // 先切出第一个 token（个数 N 那一段）
  if (n_str == NULL || sscanf(n_str, "%d", &n) != 1 || n <= 0) {
    // 解析失败或 n≤0
    printf("Usage: x N EXPR\n");
    return 0;
  }

  expr_str = n_str + strlen(n_str) + 1;   // 指针跳过 "N" 和它末尾的 '\0'，指向后面剩下的表达式串
                                          // 例如 "10 0x100" 被 strtok 切完后变成 "10\0 0x100"
                                          // n_str 指向 "10"，n_str+strlen+1 跳过 "10\0"，指向 " 0x100"
  while (*expr_str == ' ') {              // 跳过中间可能有的多余空格
    expr_str ++;
  }
  if (*expr_str == '\0') {                // 跳完空格后发现后面没有表达式了
    printf("Usage: x N EXPR\n");
    return 0;
  }

  Log("cmd_x: n=%d, expr=%s", n, expr_str);         // 调试输出：记录参数
  addr = expr(expr_str, &success);        // 把表达式算成一个"起始地址"
                                          // 例如 "0x100" 算出 0x100，"$esp+8" 算出 esp 寄存器值+8
  if (!success) {                         // 表达式求值失败（语法错误或非法）
    printf("Bad expression.\n");
    return 0;
  }

  Log("cmd_x: start=0x%08x", addr);       // 调试输出：记录起始地址
  for (i = 0; i < n; i++) {               // 连续读 N 个内存位置
    uint32_t data = vaddr_read(addr + i * 4, 4);       // 每次读 4 字节（地址每轮 +4）
                                                       // vaddr_read(地址, 长度) 是 NEMU 提供的读内存函数
    Log("cmd_x: addr=0x%08x, data=0x%08x", addr + i * 4, data);  // 调试输出
    printf("0x%08x: 0x%08x\n", addr + i * 4, data);    // 打印 "地址: 内容"
  }
  return 0;
}

static int cmd_p(char *args);       // 先声明
static int cmd_w(char *args);       // 先声明
static int cmd_d(char *args);       // 先声明

// cmd_p: 命令 "p EXPR" (print) 的处理函数 - 计算并打印表达式的值
// 用法：p 1+2*3         计算 1+2*3 的值并打印
//       p $eax          打印 eax 寄存器的值
//       p *0x100000     打印地址 0x100000 处的内存值（解引用）
static int cmd_p(char *args) {
  bool success = true;                    // 标记表达式求值是否成功
  uint32_t result;                        // 存放计算结果

  if (args == NULL) {                     // 用户没给表达式
    printf("Usage: p EXPR\n");            // 提示正确用法
    return 0;
  }

  Log("cmd_p: expr=%s", args);            // 调试输出：记录表达式
  result = expr(args, &success);          // 调用表达式求值函数（这是 PA1 最难的部分！）
  if (success) {                          // 求值成功
    printf("0x%08x (%u)\n", result, result);   // 同时按十六进制和十进制打印结果
  } else {                                // 求值失败（表达式有语法错误）
    printf("Bad expression.\n");
  }
  return 0;
}

// cmd_w: 命令 "w EXPR" (watchpoint) 的处理函数 - 设置监视点
// 用法：w $eax         监视 eax 寄存器，它的值一变化就自动暂停程序
//       w *0x100000    监视地址 0x100000 的内存值
// 监视点的原理：把表达式和它当前的值记下来，之后每执行一条指令都重新算一遍，
//              如果新值≠旧值，就停下来告诉你"值变了"
static int cmd_w(char *args) {
  if (args == NULL) {                     // 用户没给表达式
    printf("Usage: w EXPR\n");            // 提示正确用法
    return 0;
  }

  Log("cmd_w: expr=%s", args);            // 调试输出：记录表达式
  if (!add_watchpoint(args)) {            // 转交给监视点模块去创建监视点
                                          // add_watchpoint 会先求一次表达式的值，存起来当"旧值"
    printf("Bad expression.\n");          // 表达式非法（例如语法错误），创建失败
  }
  return 0;
}

// cmd_d: 命令 "d N" (delete) 的处理函数 - 删除编号为 N 的监视点
// 用法：d 0    删除编号为 0 的监视点
//       d 2    删除编号为 2 的监视点
static int cmd_d(char *args) {
  int no;                                 // 监视点编号

  if (args == NULL || sscanf(args, "%d", &no) != 1) {
    // 用户没给参数或者参数不是合法数字
    printf("Usage: d N\n");               // 提示正确用法
    return 0;
  }

  Log("cmd_d: NO=%d", no);                // 调试输出：记录编号
  if (!delete_watchpoint(no)) {           // 转交给监视点模块去删除
    printf("No such watchpoint: %d\n", no);  // 没找到这个编号的监视点
  }
  return 0;
}

// 命令表：每一项 = {命令名, 说明文字, 处理函数}
// 这是一个"表驱动"的设计：以后想加新命令，只要往这张表里加一行即可，不用写一长串 if-else
static struct {
  char *name;                 // 命令名，例如 "si"
  char *description;          // 执行 help 时显示的说明文字
  int (*handler) (char *);    // 函数指针：指向真正处理这个命令的函数
                              // 例如 handler = cmd_si，那 handler(args) 就等于调用 cmd_si(args)
} cmd_table [] = {
  { "help", "Display informations about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },
  { "si", "Execute N instructions and stop", cmd_si },
  { "info", "Print program status", cmd_info },
  { "x", "Examine memory", cmd_x },
  { "p", "Evaluate expression", cmd_p },
  { "w", "Set a watchpoint", cmd_w },
  { "d", "Delete a watchpoint", cmd_d },

  /* TODO: Add more commands */
  // 如果将来要加新命令，在这里加一行即可

};

#define NR_CMD (sizeof(cmd_table) / sizeof(cmd_table[0]))  // 命令条数 = 整张表的字节数 ÷ 一项的字节数
                                                            // 这是 C 语言里计算数组长度的经典写法
                                                            // 例如 cmd_table 有 9 项，sizeof(cmd_table) 就是 9 倍的单项大小

// cmd_help: 命令 "help" 的处理函数 - 显示帮助信息
// 用法：help       列出所有命令
//       help si    只显示 si 命令的帮助
static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");          // strtok(NULL, ...) 表示"接着上一次的分词位置继续往下取"
                                          // 因为主循环已经调用过 strtok 把命令名切出来了，这里 NULL 就是接着切参数
  int i;

  if (arg == NULL) {                      // 无参数 → help 后面没跟任何东西
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {       // 遍历整张命令表
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
      // 逐行打印：命令名 - 说明
    }
  }
  else {                                  // 有参数 → help 后面跟了一个命令名
    for (i = 0; i < NR_CMD; i ++) {       // 在命令表里查找这个名字
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;                         // 找到了就打印并返回
      }
    }
    printf("Unknown command '%s'\n", arg);  // 整张表都没匹配 → 未知命令
  }
  return 0;
}

// ui_mainloop: 调试器主循环 - 不停地"读一行 → 找对应命令 → 调它的处理函数"
// 参数 is_batch_mode：是否为批处理模式（非交互模式）
void ui_mainloop(int is_batch_mode) {
  if (is_batch_mode) {                    // 批处理模式：直接连续运行，不进交互界面
    cmd_c(NULL);                          // 相当于自动执行 c 命令（一直运行到结束）
    return;
  }

  while (1) {                             // 死循环：不停地等待用户输入
    char *str = rl_gets();                // 读入一行命令
    char *str_end = str + strlen(str);    // 指向这一行的结尾（用来判断参数是否越界）

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");         // 第一个词 = 命令名（例如 "si"）
                                          // strtok 会把字符串按空格分割，并把第一个空格改成 '\0'
    if (cmd == NULL) { continue; }        // 空行（用户只按了回车）→ 跳过，重新等待输入

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;   // 参数串 = 命令名后面那段
                                          // 例如 "si 5" 被 strtok 切完变成 "si\0 5"
                                          // cmd 指向 "si"，cmd+strlen+1 跳过 "si\0"，指向 " 5"
    if (args >= str_end) {                // 如果跳过命令名后已经到了行尾
      args = NULL;                        // 说明这条命令没有参数（例如 "c"、"q"）
    }

#ifdef HAS_IOE
    extern void sdl_clear_event_queue(void);
    sdl_clear_event_queue();              // （如果编译时开了 IOE）清空 SDL 事件队列
                                          // 防止之前积累的键盘/鼠标事件干扰后续执行
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {       // 在命令表里逐项查找名字匹配的命令
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }  // 调用它的处理函数
                                                         // 如果返回负数（只有 cmd_q 会），就退出主循环
        break;                            // 找到了就跳出查找循环
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }  // 整张表都没匹配 → 未知命令
  }
}
