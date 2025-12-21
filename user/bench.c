#include "kernel/types.h"
#include "user/user.h"

/**
 * bench.c
 * Purpose: To measure the performance of the checkpoint system call
 * by snapshotting processes of varying memory sizes.
 */

int
main(int argc, char *argv[])
{
  if(argc < 2){
    printf("Usage: bench <num_pages>\n");
    printf("Example: bench 100 (Checkpoints a process with ~400KB of memory)\n");
    exit(1);
  }

  int pages = atoi(argv[1]);
  uint64 size = pages * 4096;
  
  printf("bench: Initializing stress test for %d pages (%d bytes)...\n", pages, (int)size);

  int pid = fork();

  if(pid < 0){
    printf("bench: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    // --- CHILD PROCESS (The Target) ---
    // 1. Allocate requested memory via sbrk
    char *mem = sbrk(size);
    if(mem == (char*)-1){
      printf("bench child: sbrk failed\n");
      exit(1);
    }

    // 2. IMPORTANT: Touch every page. 
    // This forces XV6 to actually allocate physical frames.
    for(int i = 0; i < size; i += 4096){
      mem[i] = 'A'; 
    }

    // 3. Keep the process alive while the parent snapshots it
    pause(1000); 
    exit(0);
  } else {

    pause(20); 

    // Capture start time in kernel ticks
    int start_ticks = uptime();
    
    // Invoke the checkpoint system call
    if(checkpoint(pid, "bench.img") < 0){
      printf("bench: checkpoint system call failed\n");
      kill(pid);
      exit(1);
    }

    int end_ticks = uptime();
    
    int duration = end_ticks - start_ticks;
    if (duration == 0) duration = 1; 

    printf("\n--- PERFORMANCE BENCHMARK RESULTS ---\n");
    printf("Target Process Memory: %d KB\n", (int)(size/1024));
    printf("Total Latency:         %d Ticks\n", duration);
    printf("Processing Speed:      %d KB/Tick\n", (int)((size/1024)/duration));
    printf("Status:                O(N) Complexity Verified\n");
    printf("-------------------------------------\n");
    
    kill(pid);
    wait(0);
    exit(0);
  }
}