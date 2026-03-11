#include "nemu.h"
#include "monitor/monitor.h"
#include "monitor/watchpoint.h"

/* The assembly code of instructions executed is only output to the screen
 * when the number of instructions executed is less than this value.
 * This is useful when you use the `si' command.
 * You can modify this value as you want.
 */
#define MAX_INSTR_TO_PRINT 10
#define IOE_UPDATE_INTERVAL 64

int nemu_state = NEMU_STOP;

void exec_wrapper(bool);

/* Simulate how the CPU works. */
void cpu_exec(uint64_t n) {
  if (nemu_state == NEMU_END) {
    printf("Program execution has ended. To restart the program, exit NEMU and run again.\n");
    return;
  }
  nemu_state = NEMU_RUNNING;

  bool print_flag = n < MAX_INSTR_TO_PRINT;
#ifdef HAS_IOE
  extern void device_update();
  uint32_t ioe_interval = print_flag ? 1 : IOE_UPDATE_INTERVAL;
  uint32_t ioe_budget = ioe_interval;
#endif

  for (; n > 0; n --) {
    /* Execute one instruction, including instruction fetch,
     * instruction decode, and the actual execution. */
    exec_wrapper(print_flag);

#ifdef DEBUG
    if (check_watchpoints()) {
      Log("cpu_exec: stop by watchpoint at eip=0x%08x", cpu.eip);
      nemu_state = NEMU_STOP;
    }
#endif

#ifdef HAS_IOE
    // Device polling is much more expensive than a single guest instruction.
    // Keep single-step behavior unchanged, but batch updates for long runs.
    if (--ioe_budget == 0 || nemu_state != NEMU_RUNNING || n == 1) {
      device_update();
      ioe_budget = ioe_interval;
    }
#endif

    if (nemu_state != NEMU_RUNNING) { return; }
  }

  if (nemu_state == NEMU_RUNNING) { nemu_state = NEMU_STOP; }
}
