#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "Usage: restart <checkpoint_file>\n");
    exit(1);
  }

  if(restore(argv[1]) < 0){
    fprintf(2, "restart: restore failed\n");
    exit(1);
  }

  exit(0);
}