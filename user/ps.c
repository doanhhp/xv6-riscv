// user/ps.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// We define the constants and enums that the user program needs,
// instead of including the whole kernel/proc.h header.
#define NPROC 64
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))

// Helper to print process state
const char* get_state_str(int state) {
  static const char *states[] = {
    [UNUSED]    "unused",
    [USED]      "used",
    [SLEEPING]  "sleeping",
    [RUNNABLE]  "runnable",
    [RUNNING]   "running",
    [ZOMBIE]    "zombie"
  };
  if(state >= 0 && state < NELEM(states) && states[state])
    return states[state];
  return "???";
}

int
main(void)
{
  struct proc_info processes[NPROC];
  int count;

  // Call the new procinfo system call
  count = procinfo(processes);

  if (count < 0) {
    printf("ps: procinfo failed\n");
    exit(1);
  }

  // Print header
  printf("PID\tSTATE\t\tNAME\n");

  // Loop through the results and print them
  for (int i = 0; i < count; i++) {
    printf("%d\t%-10s\t%s\n", processes[i].pid, get_state_str(processes[i].state), processes[i].name);
  }

  exit(0);
}
