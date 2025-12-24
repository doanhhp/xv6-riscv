#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAGIC 0xdeadbeef

int after_restart_printed = 0;

int
main(void)
{
  int stack_counter = 0;
  int *heap_magic = malloc(sizeof(int));
  if (heap_magic == 0) {
    printf("TEST: FAIL malloc\n");
    exit(1);
  }

  *heap_magic = MAGIC;

  int pid = getpid();
  printf("TEST: PID=%d\n", pid);
  printf("TEST: START pid=%d\n", pid);

  while (1) {
    if (*heap_magic != MAGIC) {
      printf("TEST: FAIL heap_corrupt value=%x\n", *heap_magic);
      exit(1);
    }
    if (stack_counter >= 10 && after_restart_printed == 0) {
      printf("=== AFTER RESTART ===\n");
      after_restart_printed = 1;
    }

    printf("TEST: PASS stack=%d heap=%x pid=%d\n",
           stack_counter, *heap_magic, pid);

    stack_counter++;
    pause(10);
  }

  exit(0);
}
