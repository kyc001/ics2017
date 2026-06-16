// control.c - 控制流指令的执行函数（PA2 核心文件之一）
// 功能：实现 x86 的控制流指令（jmp、jcc、call、ret 等）
// 这些指令改变程序的执行顺序，是实现条件判断、循环、函数调用的基础

#include "cpu/exec.h"     // 指令执行框架的头文件

// jmp 指令：无条件跳转
// 语义：eip = 目标地址
// 用途：改变程序执行流程，跳转到指定位置
// 例如：jmp 0x8048000（跳转到绝对地址）
//       jmp label（跳转到标签，汇编器会计算相对偏移）
make_EHelper(jmp) {
  // the target address is calculated at the decode stage
  // 目标地址已经在译码阶段计算好了，存在 decoding.jmp_eip
  // 例如：jmp +10（相对跳转）→ 译码时算出 jmp_eip = 当前eip + 10
  decoding.is_jmp = 1;                          // 标记"发生了跳转"
                                                // exec_wrapper 会检查这个标志，决定是否更新 eip

  print_asm("jmp %x", decoding.jmp_eip);        // 打印汇编指令，例如："jmp 0x8048100"
}

// jcc 指令：条件跳转（Jump if Condition Code）
// 语义：如果条件成立，eip = 目标地址；否则顺序执行
// 条件由 opcode 的低 4 位决定（例如 je、jne、jl、jg 等）
// 用途：实现 if、while、for 等控制结构
// 例如：je label（如果相等就跳转，即 ZF=1 时跳转）
//       jg label（如果大于就跳转，即 ZF=0 且 SF=OF 时跳转）
make_EHelper(jcc) {
  // the target address is calculated at the decode stage
  // 目标地址已经在译码阶段算好
  uint8_t subcode = decoding.opcode & 0xf;      // 取 opcode 的低 4 位（条件码）
                                                // 0=o（溢出），1=no（未溢出），2=b（低于），3=ae（高于等于）
                                                // 4=e（相等），5=ne（不等），6=be（低于等于），7=a（高于）
                                                // 8=s（负数），9=ns（非负），a=p（奇偶），b=np（非奇偶）
                                                // c=l（小于），d=ge（大于等于），e=le（小于等于），f=g（大于）
  rtl_setcc(&t2, subcode);                      // t2 = 条件是否成立（1=成立，0=不成立）
                                                // rtl_setcc 会根据 subcode 和当前 EFLAGS 判断条件
  decoding.is_jmp = t2;                         // 如果条件成立（t2=1），标记为跳转

  print_asm("j%s %x", get_cc_name(subcode), decoding.jmp_eip);
  // get_cc_name 把条件码转换成助记符（例如 4→"e"，5→"ne"）
  // 打印例如："je 0x8048100"（如果相等就跳到 0x8048100）
}

// jmp_rm 指令：间接跳转
// 语义：eip = 目标操作数的值（不是地址，是值本身）
// 用途：跳转到一个变量或寄存器里存的地址
// 例如：jmp *%eax（跳转到 eax 寄存器里的地址）
//       jmp *(%eax)（跳转到 [eax] 内存位置存的地址）
make_EHelper(jmp_rm) {
  decoding.jmp_eip = id_dest->val;              // 目标地址 = 操作数的值
                                                // 例如：jmp *%eax → jmp_eip = eax 的值
  decoding.is_jmp = 1;                          // 标记为跳转

  print_asm("jmp *%s", id_dest->str);           // 打印例如："jmp *%eax"
                                                // 注意星号 * 表示间接寻址
}

// call 指令：函数调用
// 语义：push(返回地址); eip = 目标地址
// 返回地址 = 当前指令的下一条指令地址（decoding.seq_eip）
// 用途：调用函数，保存返回地址到栈上
// 工作流程：
//   1. 把返回地址压栈（这样被调用函数执行完后可以 ret 回来）
//   2. 跳转到目标函数
make_EHelper(call) {
  // the target address is calculated at the decode stage
  // 目标地址已经在译码阶段算好
  rtl_li(&t0, decoding.seq_eip);                // t0 = 返回地址（这条 call 指令的下一条指令地址）
                                                // seq_eip = sequential eip = 顺序执行的下一个 eip
  // Log("pa2-debug: call return=0x%08x target=0x%08x", decoding.seq_eip, decoding.jmp_eip);
  rtl_push(&t0);                                // 把返回地址压栈
                                                // 这样被调用函数执行 ret 时，会弹出这个地址继续执行
  decoding.is_jmp = 1;                          // 标记为跳转（跳转到目标函数）

  print_asm("call %x", decoding.jmp_eip);       // 打印例如："call 0x8048200"
}

// ret 指令：函数返回
// 语义：pop(eip)
// 用途：从函数返回到调用者
// 工作流程：
//   1. 从栈顶弹出返回地址
//   2. 跳转到返回地址
// 与 call 配对：call 把返回地址压栈，ret 把它弹出
make_EHelper(ret) {
  rtl_pop(&t0);                                 // 从栈顶弹出返回地址到 t0
                                                // 这个地址是之前 call 指令压入的
  // Log("pa2-debug: ret target=0x%08x", t0);
  decoding.jmp_eip = t0;                        // 跳转到返回地址
  decoding.is_jmp = 1;                          // 标记为跳转

  print_asm("ret");                             // 打印："ret"
}

// call_rm 指令：间接函数调用
// 语义：push(返回地址); eip = 目标操作数的值
// 用途：调用一个函数指针（函数地址存在变量或寄存器里）
// 例如：call *%eax（调用 eax 寄存器里的地址）
//       call *(%eax)（调用 [eax] 内存位置存的地址）
make_EHelper(call_rm) {
  rtl_li(&t0, decoding.seq_eip);                // t0 = 返回地址
  rtl_push(&t0);                                // 压栈返回地址
  decoding.jmp_eip = id_dest->val;              // 目标地址 = 操作数的值
                                                // 例如：call *%eax → 跳转到 eax 的值
  decoding.is_jmp = 1;                          // 标记为跳转

  print_asm("call *%s", id_dest->str);          // 打印例如："call *%eax"
}

// ──────────────────────────────────────────────────────────────────────────────
// 控制流指令工作原理总结：
//
// 1. 顺序执行 vs 跳转执行：
//    - 默认情况：eip = decoding.seq_eip（顺序执行下一条指令）
//    - 发生跳转：eip = decoding.jmp_eip（跳转到目标地址）
//    - decoding.is_jmp 标志决定用哪个（在 exec_wrapper 的 update_eip 中检查）
//
// 2. call 和 ret 的配对：
//    call 0x8048200:
//      push(0x8048010)       // 假设 call 指令在 0x804800b，长度 5 字节
//      jmp 0x8048200
//
//    ... 被调用函数执行 ...
//
//    ret:
//      pop(eip)              // 弹出 0x8048010
//      jmp 0x8048010         // 回到 call 的下一条指令
//
// 3. 条件跳转的典型用法：
//    cmp eax, 10           // 比较 eax 和 10，更新 EFLAGS
//    jg label              // 如果 eax > 10（ZF=0 且 SF=OF），跳转到 label
//    ... 否则执行这里 ...
//    label: ...
//
// 4. 函数调用栈帧结构：
//    高地址
//    ┌──────────────────┐
//    │  参数 n          │
//    │  ...             │
//    │  参数 1          │
//    ├──────────────────┤
//    │  返回地址        │ ← call 压入
//    ├──────────────────┤
//    │  旧的 ebp        │ ← push ebp
//    ├──────────────────┤ ← ebp 指向这里（mov ebp, esp）
//    │  局部变量 1      │
//    │  ...             │
//    │  局部变量 n      │
//    └──────────────────┘ ← esp 指向这里（栈顶）
//    低地址
// ──────────────────────────────────────────────────────────────────────────────
