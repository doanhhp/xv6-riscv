#include "kernel/types.h"
#include "user/user.h"

#define ARRAY_SIZE 500

void print_evidence(int pid, int* data, int iteration) {
  printf("\n--- EVIDENCE LOG [Iteration %d] ---\n", iteration);
  printf("Process Status: PID %d is active.\n", pid);
  printf("Memory Samples (Checking for pattern i*10 + 7):\n");
  printf("  [Index 0]    Value: %d (Expected 7)\n", data[0]);
  printf("  [Index 250]  Value: %d (Expected 2507)\n", data[250]);
  printf("  [Index 499]  Value: %d (Expected 4997)\n", data[499]);
  
  int errors = 0;
  for(int i = 0; i < ARRAY_SIZE; i++){
    if(data[i] != i * 10 + 7) errors++;
  }

  if(errors == 0) {
    printf("Result: 100%% Data Integrity Verified. No corruption detected.\n");
  } else {
    printf("Result: CRITICAL ERROR! %d memory words corrupted!\n", errors);
  }
  printf("----------------------------------\n");
}

int
main(int argc, char *argv[])
{
  int pid = getpid();
  int *data = malloc(ARRAY_SIZE * sizeof(int));
  
  if(data == 0){
    printf("integrity: malloc failed\n");
    exit(1);
  }

  // Initialize
  for(int i = 0; i < ARRAY_SIZE; i++){
    data[i] = i * 10 + 7;
  }

  printf("integrity: PID %d initialized complex memory pattern. Ready for Checkpoint.\n", pid);

  int counter = 0;
  while(1){
    print_evidence(getpid(), data, counter);
    counter++;
    pause(150); 
  }
  
  exit(0);
}