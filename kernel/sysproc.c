#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "stat.h"
#include "kernel/fs.h"
#include "kernel/sleeplock.h"
#include "kernel/file.h"
#include "kernel/fcntl.h"

// =================================================================
// STANDARD SYSTEM CALLS
// =================================================================

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
    // Lazily allocate memory
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
sys_procinfo(void)
{
  uint64 user_ptr; 
  int num_procs = 0;

  argaddr(0, &user_ptr);

  struct proc_info *kernel_buf = (struct proc_info *)kalloc();
  if(kernel_buf == 0) {
    return -1;
  }
  
  // printf("kernel: kalloc() allocated a page for procinfo\n");

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

  if(copyout(myproc()->pagetable, user_ptr, (char *)kernel_buf, num_procs * sizeof(struct proc_info)) != 0) {
    kfree((void *)kernel_buf);
    return -1;
  }
  
  // printf("kernel: freeing page used by procinfo\n");
  kfree((void *)kernel_buf);

  return num_procs;
}


// =================================================================
// CHECKPOINT & RESTORE (implemented in proc.c + vm.c)
// =================================================================

uint64
sys_checkpoint(void)
{
  int target_pid;
  char filename[64];

  argint(0, &target_pid);
  if(argstr(1, filename, sizeof(filename)) < 0)
    return -1;

  return proc_checkpoint(target_pid, filename);
}

uint64
sys_restore(void)
{
  char path[128];

  if(argstr(0, path, sizeof(path)) < 0)
    return -1;

  return proc_restore(path);
}
