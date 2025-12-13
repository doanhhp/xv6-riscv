#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "Usage: chkpt <pid> <filename>\n");
    exit(1);
  }

  int pid = atoi(argv[1]); // Convert string to int
  char *filename = argv[2];

  printf("chkpt: Checkpointing process %d to %s...\n", pid, filename);

  if(checkpoint(pid, filename) < 0){
    fprintf(2, "chkpt: Checkpoint failed!\n");
    exit(1);
  }

  printf("chkpt: Success.\n");
  exit(0);
}