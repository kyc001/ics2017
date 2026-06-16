// watchpoint.c - 监视点（Watchpoint）实现
// 功能：管理一组"要盯住的表达式"，每执行一条指令就重新算一遍，值变了就停下来
// 监视点是调试器的强大功能：可以自动监控变量/内存/寄存器的变化，不用手动单步一步步看

#include "monitor/watchpoint.h"   // 监视点结构体 WP 的定义（含 NO 编号、next 指针、expr 表达式串、old_val 旧值）
#include "monitor/expr.h"         // 表达式求值函数 expr() 的声明——监视点要靠它来算表达式

#include <string.h>               // 字符串操作函数（strncpy 等）

#define NR_WP 32      // 监视点总数上限：固定 32 个（NR = NumbeR，数量）
                      // 这是一个"静态池"的设计：预先分配 32 个位置，不用 malloc 动态申请

// 监视点"池"：一个固定大小的数组，32 个节点全在这里，不用 malloc 动态申请
// 好处：申请和释放速度快（O(1)），不会内存泄漏，也不用担心 malloc 失败
static WP wp_pool[NR_WP];

// 两个链表头指针：
//   head  = 正在使用的监视点链表（用户设置的、正在监视中的监视点）
//   free_ = 空闲节点链表（还没被占用的节点，可以借用）
// 注意：free_ 末尾加下划线，是为了避开标准库函数名 free，防止重名冲突
static WP *head, *free_;

// 从"空闲链"借一个节点出来，挂到"使用链"上，并返回这个节点
// 这就像从停车场（空闲链）开走一辆车（节点），开到正在使用的区域（使用链）
static WP* new_wp(void) {
  WP *wp;

  Assert(free_ != NULL, "No free watchpoint.");  // 万一 32 个全用光了，直接报错停下
                                                 // Assert 是 NEMU 的断言宏，条件不满足就打印错误信息并退出

  // 下面 4 步是"从链表头摘节点"的标准操作：
  wp = free_;          // ① 取空闲链的第一个节点（头节点）
  free_ = free_->next; // ② 空闲链头指针后移一位（这个节点被借走了，不再是空闲链的一部分）
  wp->next = head;     // ③ 把借来的节点插到"使用链"的最前面（头插法）
                       //    让它的 next 指向当前的使用链头
  head = wp;           // ④ 使用链头指针更新成它（现在它是使用链的新头节点）

  Log("new_wp: NO=%d", wp->NO);  // 调试输出：记录借出了哪个编号的节点
  return wp;           // 返回给调用者，调用者会填充表达式和旧值
}

// 把一个用完的节点"还回"空闲链（头插法）
// 这就像把车开回停车场：从使用链摘下来，挂回空闲链头部
static void free_wp(WP *wp) {
  Log("free_wp: NO=%d", wp->NO);  // 调试输出：记录归还了哪个编号的节点

  // 头插法把节点还回空闲链：
  wp->next = free_;    // ① 让它的 next 指向当前的空闲链头
  free_ = wp;          // ② 空闲链头指针更新成它（现在它是空闲链的新头节点）
                       // 这样它就回到池子里，可以被再次借用
}

// 程序启动时调用一次：把 32 个节点初始化，并全部串进"空闲链"
// 这个函数在 NEMU 启动时被调用，把监视点池准备好
void init_wp_pool(void) {
  int i;
  for (i = 0; i < NR_WP; i ++) {           // 逐个处理 0~31 号节点
    wp_pool[i].NO = i;                     // 给每个节点编号（显示/删除时用这个号来标识）
    wp_pool[i].next = &wp_pool[i + 1];     // 让第 i 个节点指向第 i+1 个节点
                                           // → 把整个数组串成一条单链表：0→1→2→...→31
    wp_pool[i].expr[0] = '\0';             // 表达式串先清空（首字符设成字符串结束符 '\0'）
    wp_pool[i].old_val = 0;                // 旧值先置 0
  }
  wp_pool[NR_WP - 1].next = NULL;          // 最后一个节点（第 31 个）后面没有了，指向 NULL 收尾
                                           // 这样链表就是：0→1→2→...→31→NULL

  head = NULL;                             // 一开始没有任何"正在使用"的监视点（用户还没设置任何监视点）
  free_ = wp_pool;                         // 空闲链从第 0 个节点开始 → 32 个节点全部空闲待命
}

// 命令 "w EXPR" 会调用它：新增一个监视点，盯住表达式 expr_str
// 工作流程：先算一次表达式当前的值 → 借一个节点 → 把表达式和旧值存进节点
bool add_watchpoint(char *expr_str) {
  bool success = true;                     // 标记表达式求值是否成功
  uint32_t val;                            // 存放表达式当前的值（作为"旧值"）
  WP *wp;                                  // 指向新申请的监视点节点

  val = expr(expr_str, &success);          // 先算一次表达式当前的值
                                           // 这是监视点的核心：要先知道"现在的值"，才能后面比较"是否变了"
  if (!success) {                          // 表达式非法（例如语法错误）
    Log("add_watchpoint: bad expr=%s", expr_str);  // 调试输出
    return false;                          // 不创建监视点，返回失败
  }

  wp = new_wp();                           // 从空闲链借一个节点
  strncpy(wp->expr, expr_str, sizeof(wp->expr) - 1);
  // 把表达式文字拷贝进节点
  // 用 strncpy 而不是 strcpy，防止表达式太长导致缓冲区溢出
  // sizeof(wp->expr) - 1：留一个字节给结束符 '\0'
  wp->expr[sizeof(wp->expr) - 1] = '\0';   // 手动补上结束符，确保一定是合法字符串
                                           // （strncpy 有时不会自动加 '\0'，必须手动保证）
  wp->old_val = val;                       // 记下"旧值"（刚才算出的值）
                                           // 以后每执行一条指令，check_watchpoints 会重新算表达式
                                           // 拿新值和这个 old_val 比较，不同就说明"值变了"

  Log("add_watchpoint: NO=%d expr=%s old_val=0x%08x", wp->NO, wp->expr, wp->old_val);
  printf("Watchpoint %d: %s = 0x%08x\n", wp->NO, wp->expr, wp->old_val);
  // 提示用户已创建监视点，显示编号和初始值
  return true;
}

// 命令 "d N" 会调用它：按编号 no 删除一个监视点
// 工作流程：在使用链里找到编号匹配的节点 → 从链表中摘下 → 还回空闲链
bool delete_watchpoint(int no) {
  WP *prev = NULL;                         // prev 记住当前节点的前一个节点（删除时需要它）
  WP *cur = head;                          // cur 从使用链头开始扫描

  while (cur != NULL) {                    // 遍历整条使用链
    if (cur->NO == no) {                   // 找到编号匹配的节点
      // 从链表中摘下这个节点（有两种情况：它是头节点 vs 它在中间/末尾）
      if (prev == NULL) {                  // 情况 1：它正好是链头
        head = cur->next;                  // 直接让 head 指针跳过它，指向它的下一个节点
      } else {                             // 情况 2：它在中间或末尾
        prev->next = cur->next;            // 让前一个节点跨过它，直接指向它的下一个节点
                                           // 例如：prev→cur→next 变成 prev→next
      }
      free_wp(cur);                        // 把摘下来的节点还回空闲链
      printf("Watchpoint %d deleted.\n", no);
      return true;                         // 删除成功
    }
    prev = cur;                            // 没匹配，继续往后走：prev 跟上来
    cur = cur->next;                       // cur 前进一步
  }

  Log("delete_watchpoint: NO=%d not found", no);  // 调试输出：整条链都没找到
  return false;                            // 删除失败（没有这个编号的监视点）
}

// 命令 "info w" 会调用它：打印当前所有监视点的信息
void info_watchpoints(void) {
  WP *cur = head;                          // 从使用链头开始

  if (cur == NULL) {                       // 如果使用链是空的（用户还没设置任何监视点）
    printf("No watchpoints.\n");
    return;
  }

  printf("Num\tWhat\tValue\n");            // 打印表头：编号 / 表达式 / 当前值
  while (cur != NULL) {                    // 沿使用链逐个打印
    printf("%d\t%s\t0x%08x\n", cur->NO, cur->expr, cur->old_val);
    // NO = 编号，expr = 表达式文字，old_val = 上次记录的值
    cur = cur->next;                       // 前进到下一个节点
  }
}

// ★★★ 监视点的灵魂函数 ★★★
// 每执行完一条指令就被调用一次，检查有没有监视点的值变了
// 工作原理：遍历所有监视点 → 重新算一遍表达式 → 和旧值比较 → 不同就触发
bool check_watchpoints(void) {
  WP *cur = head;                          // 从使用链头开始遍历
  bool triggered = false;                  // 记录"这一轮是否有任何监视点被触发"

  while (cur != NULL) {                    // 遍历所有"正在使用"的监视点
    bool success = true;                   // 标记表达式求值是否成功
    uint32_t new_val = expr(cur->expr, &success);
    // ★ 重新算一遍这个表达式的"新值" ★
    // 例如表达式是 "$eax"，每次算出来的值可能不同（因为 eax 会被指令修改）

    if (!success) {                        // 万一算失败（表达式突然变非法了？一般不会发生）
      Log("check_watchpoints: bad expr=%s", cur->expr);
      cur = cur->next;                     // 跳过它，继续检查下一个监视点
      continue;
    }

    if (new_val != cur->old_val) {         // ★ 新值 ≠ 旧值 → 说明它变了，触发监视点！ ★
      printf("Watchpoint %d triggered: %s\n", cur->NO, cur->expr);
      printf("Old value = 0x%08x\n", cur->old_val);  // 打印变化前的值
      printf("New value = 0x%08x\n", new_val);       // 打印变化后的值
      Log("check_watchpoints: NO=%d old=0x%08x new=0x%08x",
          cur->NO, cur->old_val, new_val);

      cur->old_val = new_val;              // ★ 把旧值更新成新值（供下次比较）★
                                           // 否则下一轮还是会用旧的 old_val 比较，会重复触发
      triggered = true;                    // 标记本轮有触发（上层据此暂停程序）
    }

    cur = cur->next;                       // 继续检查下一个监视点
  }

  return triggered;                        // 返回是否有监视点被触发
                                           // cpu_exec 会根据这个返回值决定是否暂停程序
}

// ──────────────────────────────────────────────────────────────────────────────
// 监视点工作流程总结（用一个例子串起来）：
//
// 1. 用户敲 "w $eax"
//    → cmd_w 调用 add_watchpoint("$eax")
//    → expr("$eax", &success) 算出 eax 当前值，假设是 0x12345678
//    → new_wp() 从空闲链借一个节点（假设是 0 号）
//    → 把 "$eax" 和 0x12345678 存进节点
//    → 节点挂到使用链：head→节点0
//
// 2. 用户敲 "c" 或 "si 100"，程序开始执行
//    → cpu_exec 的单步循环：每执行一条指令，就调用 check_watchpoints()
//    → check_watchpoints 遍历使用链，找到节点 0
//    → 重新 expr("$eax", &success) 算出新值，假设是 0x12345678（没变）
//    → 新值 == 旧值，不触发，继续执行下一条指令
//
// 3. 某条指令修改了 eax（例如 "mov eax, 100"）
//    → 执行完这条指令后，check_watchpoints 再次被调用
//    → expr("$eax", &success) 算出新值，现在是 0x00000064（十进制 100）
//    → 新值 ≠ 旧值（0x12345678 变成了 0x00000064）
//    → 触发！打印 "Watchpoint 0 triggered: $eax" 和新旧值
//    → 更新 old_val = 0x00000064（供下次比较）
//    → 返回 triggered = true
//    → cpu_exec 看到返回 true，暂停程序，回到调试器提示符 "(nemu) "
//
// 4. 用户敲 "d 0"
//    → cmd_d 调用 delete_watchpoint(0)
//    → 在使用链里找到节点 0，从链表中摘下
//    → free_wp(节点0) 把它还回空闲链
//    → 使用链变空：head→NULL，空闲链恢复完整
// ──────────────────────────────────────────────────────────────────────────────
