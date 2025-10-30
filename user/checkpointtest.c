#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int mypid = getpid(); // Get our own process ID
  char *filename = "mycheckpoint.chk";

  printf("checkpointtest: About to call checkpoint(pid=%d, file=%s)...\n", mypid, filename);

  // Call with arguments
  int ret = checkpoint(mypid, filename); 

  if(ret == 0) {
    printf("checkpointtest: checkpoint() returned 0 (success).\n");
  } else {
    printf("checkpointtest: checkpoint() returned %d (error).\n", ret);
  }

  exit(0);
}