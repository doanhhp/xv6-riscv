#include "kernel/types.h"
#include "user/user.h"

#define ARRAY_SIZE 1000

int
main(int argc, char *argv[])
{
  int pid = getpid();
  int *data = malloc(ARRAY_SIZE * sizeof(int));
  
  if(data == 0){
    printf("integrity: malloc failed\n");
    exit(1);
  }

  // 1. Initialize Data with a specific pattern
  printf("integrity (pid %d): Filling memory with pattern...\n", pid);
  for(int i = 0; i < ARRAY_SIZE; i++){
    data[i] = i * 10 + 7; // Example pattern: 7, 17, 27...
  }

  printf("integrity (pid %d): Data initialized. Ready for checkpoint.\n", pid);

  int counter = 0;
  while(1){
    // 2. Verify Data Integrity continuously
    int errors = 0;
    for(int i = 0; i < ARRAY_SIZE; i++){
      if(data[i] != i * 10 + 7){
        errors++;
      }
    }

    if(errors > 0){
      printf("integrity (pid %d): FATAL ERROR! Memory corrupted. %d errors found.\n", pid, errors);
    } else {
      printf("integrity (pid %d): Check %d passed. Memory perfect.\n", pid, counter);
    }

    counter++;
    sleep(200); // Wait for user to run chkpt/kill/restart
  }
  
  exit(0);
}