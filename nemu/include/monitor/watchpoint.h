#ifndef __WATCHPOINT_H__
#define __WATCHPOINT_H__

#include "common.h"

typedef struct watchpoint {
  int NO;
  struct watchpoint *next;

  char expr[128];
  uint32_t old_val;
} WP;

void init_wp_pool(void);
bool add_watchpoint(char *expr_str);
bool delete_watchpoint(int no);
void info_watchpoints(void);
bool check_watchpoints(void);

#endif
