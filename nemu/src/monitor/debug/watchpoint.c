#include "monitor/watchpoint.h"
#include "monitor/expr.h"

#include <string.h>

#define NR_WP 32

static WP wp_pool[NR_WP];
static WP *head, *free_;

static WP* new_wp(void) {
  WP *wp;

  Assert(free_ != NULL, "No free watchpoint.");
  wp = free_;
  free_ = free_->next;
  wp->next = head;
  head = wp;
  Log("new_wp: NO=%d", wp->NO);
  return wp;
}

static void free_wp(WP *wp) {
  Log("free_wp: NO=%d", wp->NO);
  wp->next = free_;
  free_ = wp;
}

void init_wp_pool(void) {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = &wp_pool[i + 1];
    wp_pool[i].expr[0] = '\0';
    wp_pool[i].old_val = 0;
  }
  wp_pool[NR_WP - 1].next = NULL;

  head = NULL;
  free_ = wp_pool;
}

bool add_watchpoint(char *expr_str) {
  bool success = true;
  uint32_t val;
  WP *wp;

  val = expr(expr_str, &success);
  if (!success) {
    Log("add_watchpoint: bad expr=%s", expr_str);
    return false;
  }

  wp = new_wp();
  strncpy(wp->expr, expr_str, sizeof(wp->expr) - 1);
  wp->expr[sizeof(wp->expr) - 1] = '\0';
  wp->old_val = val;

  Log("add_watchpoint: NO=%d expr=%s old_val=0x%08x", wp->NO, wp->expr, wp->old_val);
  printf("Watchpoint %d: %s = 0x%08x\n", wp->NO, wp->expr, wp->old_val);
  return true;
}

bool delete_watchpoint(int no) {
  WP *prev = NULL;
  WP *cur = head;

  while (cur != NULL) {
    if (cur->NO == no) {
      if (prev == NULL) {
        head = cur->next;
      } else {
        prev->next = cur->next;
      }
      free_wp(cur);
      printf("Watchpoint %d deleted.\n", no);
      return true;
    }
    prev = cur;
    cur = cur->next;
  }

  Log("delete_watchpoint: NO=%d not found", no);
  return false;
}

void info_watchpoints(void) {
  WP *cur = head;

  if (cur == NULL) {
    printf("No watchpoints.\n");
    return;
  }

  printf("Num\tWhat\tValue\n");
  while (cur != NULL) {
    printf("%d\t%s\t0x%08x\n", cur->NO, cur->expr, cur->old_val);
    cur = cur->next;
  }
}

bool check_watchpoints(void) {
  WP *cur = head;
  bool triggered = false;

  while (cur != NULL) {
    bool success = true;
    uint32_t new_val = expr(cur->expr, &success);
    if (!success) {
      Log("check_watchpoints: bad expr=%s", cur->expr);
      cur = cur->next;
      continue;
    }

    if (new_val != cur->old_val) {
      printf("Watchpoint %d triggered: %s\n", cur->NO, cur->expr);
      printf("Old value = 0x%08x\n", cur->old_val);
      printf("New value = 0x%08x\n", new_val);
      Log("check_watchpoints: NO=%d old=0x%08x new=0x%08x",
          cur->NO, cur->old_val, new_val);
      cur->old_val = new_val;
      triggered = true;
    }

    cur = cur->next;
  }

  return triggered;
}
