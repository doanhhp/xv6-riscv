#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  int pid;

  printf("sectest: Starting Integrity & Magic Test...\n");

  // ====================================================
  // TEST 1: The "Fake File" Test (Safety)
  // ====================================================
  // We try to restore a file that exists but is NOT a checkpoint.
  // "README" is a standard text file in xv6. 
  // It contains text, not our Magic Number (0x58563643).
  // The kernel MUST reject this, or the system would crash trying to load text as memory.
  
  printf("\n[Test 1] Attempting to restore invalid file 'README'...\n");
  if(restore("README") < 0){
    printf("sectest: PASS. System rejected invalid file.\n");
  } else {
    printf("sectest: FAIL. System accepted invalid file!\n");
  }

  // ====================================================
  // TEST 2: Real Checkpoint & Restore (Integrity)
  // ====================================================
  printf("\n[Test 2] Real Checkpoint/Restore...\n");
  
  // Create a shared variable on heap to verify memory restoration
  int *val = (int*)malloc(sizeof(int));
  *val = 100;

  pid = fork();
  if(pid == 0){
    // === CHILD ===
    *val = 200; // Modify value
    printf("child: Value is %d. Sleeping...\n", *val);
    
    // Wait for parent to checkpoint us
    pause(50); 
    
    // RESUME HERE AFTER RESTORE
    printf("child: Restored! Value is %d (Expected 200)\n", *val);
    
    if(*val == 200) 
      printf("child: INTEGRITY PASS\n");
    else            
      printf("child: INTEGRITY FAIL\n");
      
    exit(0);
  }

  // === PARENT ===
  pause(10); // Give child time to set *val = 200
  
  printf("parent: Checkpointing child...\n");
  checkpoint(pid, "sec.img"); // Creates a valid file with Magic + Checksum
  
  kill(pid); // Kill the original child
  wait(0);

  printf("parent: Restoring child...\n");
  restore("sec.img"); // This should succeed

  exit(0);
}