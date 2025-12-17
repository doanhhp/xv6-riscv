#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  int i = 0;

  printf("victim started. initial pid = %d\n", getpid());

  while(1){

    printf("victim (pid %d): counter = %d\n", getpid(), i);
    
    i++;
    
    pause(100);
  }

  exit(0);
}