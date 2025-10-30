#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
	int n;
	argint(0, &n);
	kexit(n);
	return 0;  // not reached
}

uint64
sys_getpid(void)
{
	return myproc()->pid;
}

uint64
sys_fork(void)
{
	return kfork();
}

uint64
sys_wait(void)
{
	uint64 p;
	argaddr(0, &p);
	return kwait(p);
}

uint64
sys_sbrk(void)
{
	uint64 addr;
	int t;
	int n;

	argint(0, &n);
	argint(1, &t);
	addr = myproc()->sz;

	if(t == SBRK_EAGER || n < 0) {
		if(growproc(n) < 0) {
			return -1;
		}
	} else {
		// Lazily allocate memory for this process: increase its memory
		// size but don't allocate memory. If the processes uses the
		// memory, vmfault() will allocate it.
		if(addr + n < addr)
			return -1;
		if(addr + n > TRAPFRAME)
			return -1;
		myproc()->sz += n;
	}
	return addr;
}

uint64
sys_pause(void)
{
	int n;
	uint ticks0;

	argint(0, &n);
	if(n < 0)
		n = 0;
	acquire(&tickslock);
	ticks0 = ticks;
	while(ticks - ticks0 < n){
		if(killed(myproc())){
			release(&tickslock);
			return -1;
		}
		sleep(&ticks, &tickslock);
	}
	release(&tickslock);
	return 0;
}

uint64
sys_kill(void)
{
	int pid;

	argint(0, &pid);
	return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
	uint xticks;

	acquire(&tickslock);
	xticks = ticks;
	release(&tickslock);
	return xticks;
}

uint64
sys_hello(void)
{
  printf("kernel: hello() called!\n");
  return 0;
}

uint64
sys_checkpoint(void)
{
  int pid;
  char filename[128];
  struct proc *p;

  // 1. Fetch the 1st argument (int PID)
  //    No return value to check!
  argint(0, &pid); 

  // 2. Fetch the 2nd argument (string filename)
  //    argstr DOES return int, so we check it.
  if(argstr(1, filename, 128) < 0) {
    printf("sys_checkpoint: ERROR failed to get filename\n");
    return -1;
  }

  // Debug print to check
  printf("sys_checkpoint: DEBUG: got pid=%d, filename=%s\n", pid, filename);

  // 3. Find the target process
  p = findproc(pid);

  if(p == 0) { // Check if findproc returned null
    printf("sys_checkpoint: ERROR: Process with PID %d not found.\n", pid);
    return -1;
  }

  // Debug print on success
  printf("sys_checkpoint: DEBUG: Found target process '%s' (PID %d)\n", p->name, p->pid);

  // CRITICAL: Release the lock findproc gave us
  release(&p->lock);

  return 0; // Return 0 for success
}
// In kernel/sysproc.c

uint64
sys_procinfo(void)
{
  uint64 user_ptr; // User-space pointer to the buffer
  int num_procs = 0;

  argaddr(0, &user_ptr);

  // Allocate one page of memory in the kernel
  struct proc_info *kernel_buf = (struct proc_info *)kalloc();
  if(kernel_buf == 0) {
    return -1;
  }
  
  // Print a message to the console confirming the allocation
  printf("kernel: kalloc() allocated a page for procinfo\n"); // <-- ADD THIS LINE

  // Loop through the process table
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state != UNUSED) {
      kernel_buf[num_procs].pid = p->pid;
      kernel_buf[num_procs].state = p->state;
      safestrcpy(kernel_buf[num_procs].name, p->name, sizeof(p->name));
      num_procs++;
    }
    release(&p->lock);
  }

  // Copy data to user space
  if(copyout(myproc()->pagetable, user_ptr, (char *)kernel_buf, num_procs * sizeof(struct proc_info)) != 0) {
    kfree((void *)kernel_buf);
    return -1;
  }
  
  // Print a message confirming the memory is being freed
  printf("kernel: freeing page used by procinfo\n"); // <-- ADD THIS LINE

  // Free the allocated kernel memory
  kfree((void *)kernel_buf);

  return num_procs;
}
