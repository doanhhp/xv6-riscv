#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  if(argc < 2){
    printf("Usage: restart <filename>\n");
    exit(1);
  }

  printf("restart: restoring from %s...\n", argv[1]);

  // Fork a child process to handle the restoration
  int pid = fork();

  if(pid < 0){
    printf("restart: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // CHILD PROCESS
    // This process will be replaced by the saved image.
    if(restore(argv[1]) < 0){
      printf("restart: failed to restore\n");
      exit(1);
    }
    // If restore succeeds, this line is never reached.
  } else {
    // PARENT PROCESS
    // Print the new PID so the user knows what to checkpoint next.
    printf("restart: Success! Process running in background with PID %d\n", pid);
    exit(0);
  }
  
  return 0;
}