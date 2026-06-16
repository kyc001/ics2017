// expr.c - 表达式求值（Expression Evaluation）
// 功能：把字符串形式的表达式（例如 "1+2*3"、"$eax+8"、"*0x100"）算成一个数值
// 这是 PA1 最难的部分！涉及词法分析、语法分析、递归求值

#include "nemu.h"     // NEMU 全局定义（cpu 状态、vaddr_read 读内存等）

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
// 我们用 POSIX 正则表达式函数来处理正则表达式（词法分析的基础工具）
// 在终端输入 "man regex" 可以查看 POSIX 正则表达式函数的详细文档
#include <stdlib.h>       // 标准库函数（strtoul 等）
#include <sys/types.h>    // 系统类型定义
#include <regex.h>        // 正则表达式库

// token 类型枚举：给每种 token 分配一个编号
// 为什么从 256 开始？因为单字符运算符直接用 ASCII 码当类型（'+' = 43, '-' = 45 等）
// 从 256 开始可以避开 ASCII 码范围（0-255），不会冲突
enum {
  TK_NOTYPE = 256, TK_EQ,      // TK_NOTYPE = 256（空格，要丢弃）, TK_EQ = 257（==）
  TK_NUM, TK_HEX, TK_REG,      // 十进制数、十六进制数、寄存器
  TK_DEREF, TK_NEG,            // 解引用（*）、取负（-），这两个是一元运算符
  TK_NEQ, TK_AND               // 不等（!=）、逻辑与（&&）

  /* TODO: Add more token types */
  // 如果将来要支持更多运算符（例如 ||、<=、>=），在这里加
};

// 规则表：每条规则 = {正则表达式, token 类型}
// 词法分析器会按这个表的顺序逐条尝试匹配，匹配成功就把对应的 token 类型记下来
static struct rule {
  char *regex;        // 正则表达式字符串（用来匹配输入字符串的某个部分）
  int token_type;     // 匹配成功后，这个部分对应的 token 类型
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */
  // 规则顺序很重要！靠前的规则优先匹配
  // 例如：十六进制 0x... 必须排在十进制 [0-9]+ 之前，否则 "0x10" 会被当成 "0" 和 "x10"

  {" +", TK_NOTYPE},            // 空格：一个或多个连续空格（词法分析时丢弃，不生成 token）
  {"\\$[a-zA-Z]+", TK_REG},     // 寄存器：$ 开头后跟字母（例如 $eax, $esp, $eip）
                                // \\ 是转义：C 字符串里的 "\\" 实际是一个反斜杠 \，传给正则引擎变成 \$
  {"0[xX][0-9a-fA-F]+", TK_HEX},// 十六进制：0x 或 0X 开头，后跟一个或多个十六进制数字
                                // ★ 必须在十进制规则之前！否则 "0x10" 会先匹配 "0"，剩下 "x10" 无法匹配
  {"[0-9]+", TK_NUM},           // 十进制：一个或多个连续数字（例如 123, 456）
  {"\\+", '+'},                 // 加号：token 类型直接用字符 '+' 的 ASCII 码（43）
  {"-", '-'},                   // 减号：token 类型是 '-' 的 ASCII 码（45）
  {"\\*", '*'},                 // 星号：token 类型是 '*' 的 ASCII 码（42）
                                // 注意：词法分析时 * 既可能是乘法，也可能是解引用（一元运算符）
                                // 这里先统一识别成 '*'，后面会根据上下文改写成 TK_DEREF
  {"/", '/'},                   // 除号：token 类型是 '/' 的 ASCII 码（47）
  {"\\(", '('},                 // 左括号：token 类型是 '(' 的 ASCII 码（40）
  {"\\)", ')'},                 // 右括号：token 类型是 ')' 的 ASCII 码（41）
  {"==", TK_EQ},                // 等于：双字符运算符，token 类型是枚举 TK_EQ
  {"!=", TK_NEQ},               // 不等：双字符运算符，token 类型是枚举 TK_NEQ
  {"&&", TK_AND}                // 逻辑与：双字符运算符，token 类型是枚举 TK_AND
};

#define NR_REGEX (sizeof(rules) / sizeof(rules[0]))  // 规则条数 = 整张表的字节数 ÷ 一条规则的字节数

static regex_t re[NR_REGEX];    // 编译后的正则表达式数组（每个规则对应一个编译后的正则对象）

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
// 初始化正则表达式：把 rules 表里的每条正则字符串编译成可执行的正则对象
// 正则表达式会被多次使用（每次词法分析都要用），所以只在程序启动时编译一次，提高效率
void init_regex() {
  int i;
  char error_msg[128];    // 错误信息缓冲区
  int ret;                // regcomp 的返回值（0=成功，非0=失败）

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    // regcomp：编译正则表达式字符串 rules[i].regex，结果存到 re[i]
    // REG_EXTENDED：使用扩展正则语法（支持 +、? 等元字符）
    if (ret != 0) {       // 编译失败（正则表达式语法错误）
      regerror(ret, &re[i], error_msg, 128);  // 获取错误信息
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
      // panic：NEMU 的错误报告宏，打印错误信息并退出程序
    }
  }
}

// Token 结构体：表示一个"词法单元"
typedef struct token {
  int type;         // token 类型（例如 TK_NUM、'+'、TK_REG 等）
  char str[32];     // token 的原始文字（只有数字、十六进制、寄存器需要保存，运算符不需要）
                    // 例如：数字 "123" 需要保存原文，后面要用 strtoul 转成整数
                    //       寄存器 "$eax" 需要保存，后面要根据名字查对应的寄存器值
                    //       运算符 '+' 不需要保存，只需要类型就够了
} Token;

static Token tokens[32];    // token 数组：词法分析的结果存在这里（最多 32 个 token）
static int nr_token;        // 实际生成的 token 数量

// 函数声明（具体实现在后面）
static bool check_parentheses(int p, int q);       // 检查 tokens[p..q] 是否被最外层一对括号包裹
static bool is_operator(int type);                 // 判断一个 token 类型是否为运算符
static int precedence(int type);                   // 返回运算符的优先级（数值越小优先级越低）
static int dominant_operator(int p, int q);        // 找出 tokens[p..q] 的主运算符（优先级最低、最靠右）
static uint32_t eval(int p, int q, bool *success);// 递归求值：计算 tokens[p..q] 表达式的值
static uint32_t eval_reg(const char *s, bool *success);  // 求寄存器的值（例如 "$eax" → cpu.eax）

// make_token: 词法分析（Lexical Analysis）
// 功能：把输入字符串 e 切成一个个 token，存入 tokens[] 数组
// 例如："1+2*3" → [TK_NUM"1", '+', TK_NUM"2", '*', TK_NUM"3"]
static bool make_token(char *e) {
  int position = 0;       // 当前扫描到输入字符串的哪个位置
  int i;
  regmatch_t pmatch;      // 正则匹配结果：包含匹配的起始位置和长度

  nr_token = 0;           // 清空 token 数组

  while (e[position] != '\0') {     // 只要还没到字符串结尾
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      // 按规则表的顺序逐条尝试匹配
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        // regexec：用编译好的正则 re[i] 去匹配字符串 e+position（从当前位置开始）
        // 参数：正则对象、字符串、最多匹配几个、结果、标志位
        // 返回 0 表示匹配成功
        // pmatch.rm_so == 0 这个条件至关重要！确保匹配发生在**当前位置的开头**
        // 否则 regexec 可能在后面某处匹配，导致乱序
        // 例如：输入 "1+2"，如果不检查 rm_so，可能跳过 "1" 直接匹配到 "+"
        char *substr_start = e + position;      // 匹配到的子串起始位置
        int substr_len = pmatch.rm_eo;          // 匹配到的子串长度（rm_eo = 结束偏移）

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);
        // 调试输出：记录匹配了哪条规则、在哪个位置、长度多少、内容是什么
        position += substr_len;                 // 位置前进（跳过已匹配的部分）

        switch (rules[i].token_type) {
          case TK_NOTYPE:     // 空格：直接丢弃，不生成 token
            break;
          case TK_NUM:        // 十进制数、十六进制数、寄存器：需要保存原始文字
          case TK_HEX:
          case TK_REG:
            Assert(nr_token < 32, "too many tokens");           // 超过数组上限就报错
            Assert(substr_len < (int)sizeof(tokens[nr_token].str), "token too long");
            tokens[nr_token].type = rules[i].token_type;        // 记下类型
            memcpy(tokens[nr_token].str, substr_start, substr_len);  // 拷贝原始文字
            tokens[nr_token].str[substr_len] = '\0';            // 手动加结束符（memcpy 不会自动加）
            nr_token ++;
            Log("token[%d]: type=%d str=%s", nr_token - 1,
                tokens[nr_token - 1].type, tokens[nr_token - 1].str);
            break;
          default:            // 运算符和括号：只记类型，不需要保存原文
            Assert(nr_token < 32, "too many tokens");
            tokens[nr_token].type = rules[i].token_type;
            tokens[nr_token].str[0] = '\0';                     // str 置空（用不到）
            nr_token ++;
            Log("token[%d]: type=%d", nr_token - 1, tokens[nr_token - 1].type);
            break;
        }

        break;              // 匹配成功就跳出 for 循环，继续处理下一个位置
      }
    }

    if (i == NR_REGEX) {    // 所有规则都不匹配（输入有非法字符）
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      // 打印错误位置和指示箭头，例如：
      //   no match at position 5
      //   1+2*@3
      //       ^
      return false;
    }
  }

  return true;              // 词法分析成功
}

// eval_reg: 求寄存器的值
// 例如："$eax" → cpu.eax 的值，"$eip" → cpu.eip 的值
static uint32_t eval_reg(const char *s, bool *success) {
  int i;

  if (strcmp(s, "$eip") == 0) {           // 特殊处理 eip（它不在 gpr 数组里）
    Log("eval_reg: %s -> 0x%08x", s, cpu.eip);
    return cpu.eip;
  }

  for (i = 0; i < 8; i ++) {              // 遍历 8 个通用寄存器
    char buf[8];
    sprintf(buf, "$%s", regsl[i]);        // 拼出 "$eax", "$ecx" 等
    if (strcmp(s, buf) == 0) {            // 名字匹配
      Log("eval_reg: %s -> 0x%08x", s, reg_l(i));
      return reg_l(i);                    // 返回第 i 个寄存器的值
    }
  }

  Log("eval_reg: unknown register %s", s);  // 没匹配任何寄存器名（例如 "$abc"）
  *success = false;                       // 标记失败
  return 0;
}

// is_operator: 判断一个 token 类型是否为运算符
static bool is_operator(int type) {
  return type == '+' || type == '-' || type == '*' || type == '/' ||
         type == TK_EQ || type == TK_NEQ || type == TK_AND ||
         type == TK_DEREF || type == TK_NEG;
  // 包括二元运算符（+、-、*、/、==、!=、&&）和一元运算符（TK_DEREF 解引用、TK_NEG 取负）
}

// expr: 表达式求值的入口函数（被 ui.c 的 cmd_p、cmd_x、watchpoint.c 调用）
// 参数：e = 表达式字符串（例如 "1+2*3"、"$eax+8"），success = 指向成功标志的指针
// 返回：表达式的值（如果 *success 为 false，返回值无意义）
uint32_t expr(char *e, bool *success) {
  int i;

  if (!make_token(e)) {                   // 第一步：词法分析（把字符串切成 token）
    *success = false;                     // 词法分析失败（有非法字符）
    return 0;
  }

  // 第二步：区分一元运算符（解引用 * vs 乘法 *，取负 - vs 减法 -）
  // 判定规则：一个 * 或 -，只要它**前面没有一个"可以当左操作数的值"**，它就是一元运算符
  for (i = 0; i < nr_token; i ++) {
    if (tokens[i].type == '*') {
      // 如果满足以下任一条件，这个 * 是解引用（一元），不是乘法（二元）：
      //   1. i == 0：它在最开头（例如 "*p"）
      //   2. 前一个是 '('：左括号后面不可能是乘法的左操作数（例如 "(*p)"）
      //   3. 前一个是运算符：运算符后面不可能是乘法的左操作数（例如 "1+*p"）
      if (i == 0 || tokens[i - 1].type == '(' || is_operator(tokens[i - 1].type)) {
        tokens[i].type = TK_DEREF;        // 改写成 TK_DEREF（解引用）
        Log("rewrite token[%d] to TK_DEREF", i);
      }
      // 否则（前面是数字、寄存器、右括号 ')'）就是普通的乘法，保持 '*' 不变
    }
    else if (tokens[i].type == '-') {
      // 同样的规则判断负号（一元）vs 减法（二元）
      if (i == 0 || tokens[i - 1].type == '(' || is_operator(tokens[i - 1].type)) {
        tokens[i].type = TK_NEG;          // 改写成 TK_NEG（取负）
        Log("rewrite token[%d] to TK_NEG", i);
      }
    }
  }

  if (nr_token == 0) {                    // 空表达式（例如用户输入 "p   "，只有空格）
    *success = false;
    return 0;
  }

  *success = true;                        // 假设成功（eval 函数可能会改成 false）
  return eval(0, nr_token - 1, success);  // 第三步：递归求值（计算 tokens[0..nr_token-1] 的值）
}

// check_parentheses: 检查 tokens[p..q] 是否被**最外层一对**括号包裹
// 例如："(1+2)" → true，"(1)+(2)" → false（中途括号匹配了，不是最外层一对）
static bool check_parentheses(int p, int q) {
  int balance = 0;        // 括号平衡计数器：遇 '(' +1，遇 ')' -1
  int i;

  if (tokens[p].type != '(' || tokens[q].type != ')') {
    return false;         // 首尾不是 '(' 和 ')'，肯定不是被括号包裹
  }

  for (i = p; i <= q; i ++) {
    if (tokens[i].type == '(') {
      balance ++;         // 左括号：计数器 +1
    }
    else if (tokens[i].type == ')') {
      balance --;         // 右括号：计数器 -1
      if (balance == 0 && i < q) {
        // ★ 关键检查：如果**中途**就归零了（还没到末尾 q），说明不是最外层一对 ★
        // 例如："(1+2)+(3+4)"，扫到第 5 个字符 ')' 时 balance 归零，但 i < q
        // 说明最外层不是一对括号，而是两组并列的括号
        return false;
      }
    }

    if (balance < 0) {
      return false;       // 某个右括号没有对应的左括号（例如 ")1+2("）
    }
  }

  return balance == 0;    // 最后 balance 必须是 0（左右括号数量相等）
}

// precedence: 返回运算符的优先级
// 优先级数值：数值越小优先级越低（越后算），数值越大优先级越高（越先算）
// 这个顺序决定了表达式的求值顺序
static int precedence(int type) {
  switch (type) {
    case TK_AND: return 1;            // 逻辑与 &&：优先级最低（最后算）
    case TK_EQ:                       // 相等 ==
    case TK_NEQ: return 2;            // 不等 !=：优先级第二低
    case '+':                         // 加法
    case '-': return 3;               // 减法：优先级中等
    case '*':                         // 乘法
    case '/': return 4;               // 除法：优先级较高（先乘除，后加减）
    case TK_DEREF:                    // 解引用 *
    case TK_NEG: return 5;            // 取负 -：一元运算符，优先级最高（最先算）
    default: return 0;                // 非运算符（数字、括号等）返回 0
  }
  // 优先级从低到高：&& < == != < + - < * / < 一元运算符
}

// dominant_operator: 找出 tokens[p..q] 的主运算符（main operator）
// 主运算符 = **优先级最低**（最后才算）、且**最靠右**的运算符（保证左结合）
// 为什么是"优先级最低"？因为最后算的运算符，正好就是把整个式子劈成左右两半的那"一刀"
// 为什么是"最靠右"？保证左结合：例如 "a-b-c" 要先算 a-b 再减 c，所以取最右的 '-'
static int dominant_operator(int p, int q) {
  int level = 0;          // 括号深度：遇 '(' +1，遇 ')' -1
  int op = -1;            // 记录找到的主运算符位置（初始 -1 表示还没找到）
  int best_precedence = 100;  // 记录当前找到的最低优先级（初始设成一个很大的数）
  int i;

  for (i = p; i <= q; i ++) {
    if (tokens[i].type == '(') {
      level ++;           // 进入更深一层括号
      continue;
    }
    if (tokens[i].type == ')') {
      level --;           // 退出一层括号
      continue;
    }

    if (level != 0) {     // ★ 关键：只看括号层数为 0 的运算符（括号内的运算符不算）★
      continue;           // 例如："1+(2*3)"，括号内的 '*' 不能当主运算符，只有外面的 '+' 才算
    }

    // 现在 tokens[i] 在括号层数 0，判断它是否为运算符
    if (precedence(tokens[i].type) != 0 &&              // 是运算符（precedence 返回非 0）
        precedence(tokens[i].type) <= best_precedence) {// 优先级 ≤ 当前最低（取更低的）
      best_precedence = precedence(tokens[i].type);     // 更新最低优先级
      op = i;                                           // 更新主运算符位置
      // 注意：这里用 <=（不是 <），所以相同优先级时后面的会覆盖前面的
      // 保证取"最右"的同优先级运算符（实现左结合）
    }
  }

  Log("dominant_operator: p=%d q=%d op=%d", p, q, op);
  return op;              // 返回主运算符的位置（如果没找到返回 -1）
}

// eval: 递归求值函数（表达式求值的核心！）
// 功能：计算 tokens[p..q] 表达式的值
// 算法：分治递归（divide and conquer）
//   1. 单个 token → 直接返回它的值
//   2. 被最外层括号包裹 → 去掉括号，递归算里面的
//   3. 否则 → 找主运算符，以它为界拆成左右两半，递归算左右，最后合并
static uint32_t eval(int p, int q, bool *success) {
  uint32_t val1, val2, result;    // val1=左操作数, val2=右操作数, result=结果
  int op;                         // 主运算符位置

  Log("eval enter: p=%d q=%d", p, q);   // 调试输出：记录递归进入

  if (p > q) {                    // 非法区间（例如空表达式）
    *success = false;
    return 0;
  }

  // 情况 1：单个 token（递归的基本情况 base case）
  if (p == q) {
    if (tokens[p].type == TK_NUM || tokens[p].type == TK_HEX) {
      // 数字（十进制或十六进制）：用 strtoul 转成整数
      result = strtoul(tokens[p].str, NULL, 0);
      // strtoul 参数：字符串、结束位置指针（NULL表示不需要）、进制（0表示自动识别）
      // 自动识别：0x开头→16进制，否则→10进制
      Log("eval leave: p=%d q=%d result=0x%08x", p, q, result);
      return result;
    }

    if (tokens[p].type == TK_REG) {
      // 寄存器：调用 eval_reg 求寄存器的值
      result = eval_reg(tokens[p].str, success);
      if (*success) {
        Log("eval leave: p=%d q=%d result=0x%08x", p, q, result);
      }
      return result;
    }

    // 既不是数字也不是寄存器（例如单个运算符 '+'，非法）
    *success = false;
    Log("eval leave: p=%d q=%d bad single token type=%d", p, q, tokens[p].type);
    return 0;
  }

  // 情况 2：整体被最外层一对括号包裹（例如 "(1+2)"）
  if (check_parentheses(p, q)) {
    result = eval(p + 1, q - 1, success);   // 去掉外层括号，递归算里面的
    if (*success) {
      Log("eval leave: p=%d q=%d result=0x%08x", p, q, result);
    }
    return result;
  }

  // 情况 3：找主运算符，以它为界拆成左右两半
  op = dominant_operator(p, q);   // 找出优先级最低、最靠右的运算符
  if (op < 0) {                   // 没找到运算符（例如 "123abc"，非法表达式）
    *success = false;
    return 0;
  }

  // 情况 3a：主运算符是一元运算符（TK_NEG 取负、TK_DEREF 解引用）
  // 一元运算符只有右操作数，没有左操作数
  if (tokens[op].type == TK_NEG || tokens[op].type == TK_DEREF) {
    val2 = eval(op + 1, q, success);    // 递归算右边的值
    if (!*success) {
      return 0;                   // 右边算失败，直接返回
    }

    switch (tokens[op].type) {
      case TK_NEG:                // 取负：返回 -val2
        result = -val2;
        break;
      case TK_DEREF:              // 解引用：把 val2 当作地址，读取该地址的内存值
        result = vaddr_read(val2, 4);
        // vaddr_read(地址, 长度)：读取虚拟地址处的内存
        // 例如：*0x100000 表示读取地址 0x100000 处的 4 字节内容
        Log("deref: addr=0x%08x data=0x%08x", val2, result);
        break;
      default:
        *success = false;
        return 0;
    }

    Log("eval leave: p=%d q=%d result=0x%08x", p, q, result);
    return result;
  }

  // 情况 3b：主运算符是二元运算符（+、-、*、/、==、!=、&&）
  // 二元运算符有左右两个操作数
  val1 = eval(p, op - 1, success);      // 递归算左半边的值
  if (!*success) {
    return 0;                     // 左边算失败，直接返回
  }

  val2 = eval(op + 1, q, success);      // 递归算右半边的值
  if (!*success) {
    return 0;                     // 右边算失败，直接返回
  }

  // 根据运算符类型合并左右两边的值
  switch (tokens[op].type) {
    case '+': result = val1 + val2; break;      // 加法
    case '-': result = val1 - val2; break;      // 减法
    case '*': result = val1 * val2; break;      // 乘法
    case '/':                                   // 除法
      if (val2 == 0) {                          // 除零保护
        *success = false;
        return 0;
      }
      result = val1 / val2;
      break;
    case TK_EQ: result = (val1 == val2); break; // 相等：返回 1（真）或 0（假）
    case TK_NEQ: result = (val1 != val2); break;// 不等：返回 1（真）或 0（假）
    case TK_AND: result = (val1 && val2); break;// 逻辑与：返回 1（真）或 0（假）
    default:
      *success = false;
      return 0;
  }

  Log("eval leave: p=%d q=%d result=0x%08x", p, q, result);
  return result;
}

// ──────────────────────────────────────────────────────────────────────────────
// 表达式求值完整流程示例：计算 "1+2*3"
//
// 1. expr("1+2*3", &success) 被调用
//
// 2. make_token("1+2*3") 词法分析：
//    tokens[0] = {type: TK_NUM, str: "1"}
//    tokens[1] = {type: '+', str: ""}
//    tokens[2] = {type: TK_NUM, str: "2"}
//    tokens[3] = {type: '*', str: ""}
//    tokens[4] = {type: TK_NUM, str: "3"}
//    nr_token = 5
//
// 3. 一元运算符识别：没有需要改写的（'*' 前面是数字 '2'，所以是乘法不是解引用）
//
// 4. eval(0, 4, &success) 递归求值：
//    - p=0, q=4，不是单个 token，不是被括号包裹
//    - dominant_operator(0, 4)：找主运算符
//      * i=0: tokens[0]=TK_NUM，不是运算符，跳过
//      * i=1: tokens[1]='+'，优先级=3，best_precedence=3，op=1
//      * i=2: tokens[2]=TK_NUM，不是运算符，跳过
//      * i=3: tokens[3]='*'，优先级=4 > 3，不更新（我们要找最低优先级）
//      * i=4: tokens[4]=TK_NUM，不是运算符，跳过
//      * 返回 op=1（'+' 是主运算符，因为它优先级最低，最后算）
//    - 以 '+' 为界拆成左右两半：
//      * 左边：eval(0, 0, &success) = "1" = 1
//      * 右边：eval(2, 4, &success) = "2*3"
//        + dominant_operator(2, 4) 找到 op=3（'*'）
//        + 左边：eval(2, 2, &success) = "2" = 2
//        + 右边：eval(4, 4, &success) = "3" = 3
//        + 合并：2 * 3 = 6
//      * 合并：1 + 6 = 7
//    - 返回 7
//
// 5. expr 返回 7，打印 "0x00000007 (7)"
// ──────────────────────────────────────────────────────────────────────────────
