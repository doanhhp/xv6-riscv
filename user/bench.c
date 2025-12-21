#include "kernel/types.h"
#include "user/user.h"

// Usage: bench <pages_count>
// Example: bench 100  (Allocates 100 pages ~400KB and checkpoints it)

int
main(int argc, char *argv[])
{
  if(argc < 2){
    printf("Usage: bench <num_pages>\n");
    exit(1);
  }

  int pages = atoi(argv[1]);
  int size = pages * 4096;
  
  printf("bench: Starting benchmark for %d pages (%d bytes)...\n", pages, size);

  int pid = fork();

  if(pid < 0){
    printf("bench: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // CHILD (The Victim)
    // 1. Allocate memory
    char *mem = sbrk(size);
    if(mem == (char*)-1){
      printf("bench child: sbrk failed\n");
      exit(1);
    }

    // 2. Touch every page to ensure it is physically mapped (force allocation)
    for(int i = 0; i < size; i += 4096){
      mem[i] = 'X';
    }

    // 3. Wait to be checkpointed
    sleep(500); 
    exit(0);
  } else {
    // PARENT (The Tester)
    sleep(10); // Give child time to allocate memory

    int start_ticks = uptime();
    
    // Checkpoint the child
    if(checkpoint(pid, "bench.img") < 0){
      printf("bench: checkpoint failed\n");
      kill(pid);
      exit(1);
    }

    int end_ticks = uptime();
    
    printf("bench: Result for %d pages -> Time: %d ticks\n", pages, end_ticks - start_ticks);
    
    // Cleanup
    kill(pid);
    wait(0);
    exit(0);
  }
}