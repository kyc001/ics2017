#include "nemu.h"

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <stdlib.h>
#include <sys/types.h>
#include <regex.h>

enum {
  TK_NOTYPE = 256, TK_EQ,
  TK_NUM, TK_HEX, TK_REG,
  TK_DEREF, TK_NEG

  /* TODO: Add more token types */
};

static struct rule {
  char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */

  {" +", TK_NOTYPE},            // spaces
  {"\\$[a-zA-Z]+", TK_REG},
  {"0[xX][0-9a-fA-F]+", TK_HEX},
  {"[0-9]+", TK_NUM},
  {"\\+", '+'},                 // plus
  {"-", '-'},
  {"\\*", '*'},
  {"/", '/'},
  {"\\(", '('},
  {"\\)", ')'},
  {"==", TK_EQ}                 // equal
};

#define NR_REGEX (sizeof(rules) / sizeof(rules[0]))

static regex_t re[NR_REGEX];

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static Token tokens[32];
static int nr_token;

static bool check_parentheses(int p, int q);
static bool is_operator(int type);
static int precedence(int type);
static int dominant_operator(int p, int q);
static uint32_t eval(int p, int q, bool *success);
static uint32_t eval_reg(const char *s, bool *success);

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);
        position += substr_len;

        switch (rules[i].token_type) {
          case TK_NOTYPE:
            break;
          case TK_NUM:
          case TK_HEX:
          case TK_REG:
            Assert(nr_token < 32, "too many tokens");
            Assert(substr_len < (int)sizeof(tokens[nr_token].str), "token too long");
            tokens[nr_token].type = rules[i].token_type;
            memcpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            nr_token ++;
            Log("token[%d]: type=%d str=%s", nr_token - 1,
                tokens[nr_token - 1].type, tokens[nr_token - 1].str);
            break;
          default:
            Assert(nr_token < 32, "too many tokens");
            tokens[nr_token].type = rules[i].token_type;
            tokens[nr_token].str[0] = '\0';
            nr_token ++;
            Log("token[%d]: type=%d", nr_token - 1, tokens[nr_token - 1].type);
            break;
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}

static uint32_t eval_reg(const char *s, bool *success) {
  int i;

  if (strcmp(s, "$eip") == 0) {
    Log("eval_reg: %s -> 0x%08x", s, cpu.eip);
    return cpu.eip;
  }

  for (i = 0; i < 8; i ++) {
    char buf[8];
    sprintf(buf, "$%s", regsl[i]);
    if (strcmp(s, buf) == 0) {
      Log("eval_reg: %s -> 0x%08x", s, reg_l(i));
      return reg_l(i);
    }
  }

  Log("eval_reg: unknown register %s", s);
  *success = false;
  return 0;
}

static bool is_operator(int type) {
  return type == '+' || type == '-' || type == '*' || type == '/' ||
         type == TK_EQ || type == TK_DEREF || type == TK_NEG;
}

uint32_t expr(char *e, bool *success) {
  int i;

  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  for (i = 0; i < nr_token; i ++) {
    if (tokens[i].type == '*') {
      if (i == 0 || tokens[i - 1].type == '(' || is_operator(tokens[i - 1].type)) {
        tokens[i].type = TK_DEREF;
        Log("rewrite token[%d] to TK_DEREF", i);
      }
    }
    else if (tokens[i].type == '-') {
      if (i == 0 || tokens[i - 1].type == '(' || is_operator(tokens[i - 1].type)) {
        tokens[i].type = TK_NEG;
        Log("rewrite token[%d] to TK_NEG", i);
      }
    }
  }

  if (nr_token == 0) {
    *success = false;
    return 0;
  }

  *success = true;
  return eval(0, nr_token - 1, success);
}

static bool check_parentheses(int p, int q) {
  int balance = 0;
  int i;

  if (tokens[p].type != '(' || tokens[q].type != ')') {
    return false;
  }

  for (i = p; i <= q; i ++) {
    if (tokens[i].type == '(') {
      balance ++;
    }
    else if (tokens[i].type == ')') {
      balance --;
      if (balance == 0 && i < q) {
        return false;
      }
    }

    if (balance < 0) {
      return false;
    }
  }

  return balance == 0;
}

static int precedence(int type) {
  switch (type) {
    case TK_EQ: return 1;
    case '+':
    case '-': return 2;
    case '*':
    case '/': return 3;
    case TK_DEREF:
    case TK_NEG: return 4;
    default: return 0;
  }
}

static int dominant_operator(int p, int q) {
  int level = 0;
  int op = -1;
  int best_precedence = 100;
  int i;

  for (i = p; i <= q; i ++) {
    if (tokens[i].type == '(') {
      level ++;
      continue;
    }
    if (tokens[i].type == ')') {
      level --;
      continue;
    }

    if (level != 0) {
      continue;
    }

    if (precedence(tokens[i].type) != 0 &&
        precedence(tokens[i].type) <= best_precedence) {
      best_precedence = precedence(tokens[i].type);
      op = i;
    }
  }

  Log("dominant_operator: p=%d q=%d op=%d", p, q, op);
  return op;
}

static uint32_t eval(int p, int q, bool *success) {
  uint32_t val1, val2, result;
  int op;

  Log("eval enter: p=%d q=%d", p, q);

  if (p > q) {
    *success = false;
    return 0;
  }

  if (p == q) {
    if (tokens[p].type == TK_NUM || tokens[p].type == TK_HEX) {
      result = strtoul(tokens[p].str, NULL, 0);
      Log("eval leave: p=%d q=%d result=0x%08x", p, q, result);
      return result;
    }

    if (tokens[p].type == TK_REG) {
      result = eval_reg(tokens[p].str, success);
      if (*success) {
        Log("eval leave: p=%d q=%d result=0x%08x", p, q, result);
      }
      return result;
    }

    *success = false;
    Log("eval leave: p=%d q=%d bad single token type=%d", p, q, tokens[p].type);
    return 0;
  }

  if (check_parentheses(p, q)) {
    result = eval(p + 1, q - 1, success);
    if (*success) {
      Log("eval leave: p=%d q=%d result=0x%08x", p, q, result);
    }
    return result;
  }

  op = dominant_operator(p, q);
  if (op < 0) {
    *success = false;
    return 0;
  }

  if (tokens[op].type == TK_NEG || tokens[op].type == TK_DEREF) {
    val2 = eval(op + 1, q, success);
    if (!*success) {
      return 0;
    }

    switch (tokens[op].type) {
      case TK_NEG:
        result = -val2;
        break;
      case TK_DEREF:
        result = vaddr_read(val2, 4);
        Log("deref: addr=0x%08x data=0x%08x", val2, result);
        break;
      default:
        *success = false;
        return 0;
    }

    Log("eval leave: p=%d q=%d result=0x%08x", p, q, result);
    return result;
  }

  val1 = eval(p, op - 1, success);
  if (!*success) {
    return 0;
  }

  val2 = eval(op + 1, q, success);
  if (!*success) {
    return 0;
  }

  switch (tokens[op].type) {
    case '+': result = val1 + val2; break;
    case '-': result = val1 - val2; break;
    case '*': result = val1 * val2; break;
    case '/':
      if (val2 == 0) {
        *success = false;
        return 0;
      }
      result = val1 / val2;
      break;
    case TK_EQ: result = (val1 == val2); break;
    default:
      *success = false;
      return 0;
  }

  Log("eval leave: p=%d q=%d result=0x%08x", p, q, result);
  return result;
}
