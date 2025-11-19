#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char *filename = "mycheckpoint.chk";

  // Allow passing a different filename as an argument
  if(argc > 1) {
    filename = argv[1];
  }

  printf("restoretest: About to call restore('%s')...\n", filename);

  // Call the system call
  int ret = restore(filename);

  // Note: When fully implemented, a successful restore() will NEVER return 
  // (because this process will be replaced by the saved one).
  // But for now, our stub returns 0.
  if(ret == 0) {
    printf("restoretest: restore() returned 0 (stub success).\n");
  } else {
    printf("restoretest: restore() returned %d (error).\n", ret);
  }

  exit(0);
}