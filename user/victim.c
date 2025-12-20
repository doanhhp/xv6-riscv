#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  int i = 0;

  while(1){
    printf("victim counter = %d\n", i);
    i++;
    pause(10);
  }

  exit(0);
}