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
// CHECKPOINT & RESTORE IMPLEMENTATION
// =================================================================

struct chkpt_header {
  int pid;
  uint64 sz;       // Exact memory size
  char name[16];   // Save name to restore identity
};

uint64
sys_checkpoint(void)
{
  int pid;
  char path[128];
  
  // Local Snapshot Variables
  struct chkpt_header h;
  struct trapframe tf_snapshot;
  pagetable_t victim_pagetable = 0;
  int found = 0;
  
  struct inode *ip;

  // 1. Get Arguments
  argint(0, &pid);
  if(argstr(1, path, 128) < 0)
    return -1;

  // STABILITY CHECK: Prevent checkpointing init or self
  if(pid == 1){
    printf("chkpt: error - cannot checkpoint init (pid 1)\n");
    return -1;
  }
  if(pid == myproc()->pid){
    printf("chkpt: error - process cannot checkpoint itself\n");
    return -1;
  }

  // 2. Find Victim and Snapshot State (ATOMICALLY)
  extern struct proc proc[NPROC];
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      // SNAPSHOT DATA
      h.pid = p->pid;
      h.sz = p->sz;
      safestrcpy(h.name, p->name, sizeof(h.name)); // Save name
      victim_pagetable = p->pagetable;
      
      // Copy trapframe safely (struct copy)
      if(p->trapframe)
        tf_snapshot = *(p->trapframe);
      else
        memset(&tf_snapshot, 0, sizeof(tf_snapshot));
        
      found = 1;
      
      release(&p->lock); // <--- RELEASE LOCK NOW!
      break; 
    }
    release(&p->lock);
  }

  if(!found){
    printf("chkpt: pid %d not found\n", pid);
    return -1;
  }

  // Allocate a zero-filled page for unmapped regions (e.g. guard pages or lazy alloc)
  char *zero_page = kalloc();
  if(zero_page == 0) {
    printf("chkpt: kalloc failed\n");
    return -1;
  }
  memset(zero_page, 0, PGSIZE);

  // 3. Open File (Now safe to sleep/IO)
  begin_op();
  if((ip = create(path, T_FILE, 0, 0)) == 0){
    printf("chkpt: create file failed\n");
    end_op();
    kfree(zero_page);
    return -1;
  }
  
  // 4. Write Header
  if(writei(ip, 0, (uint64)&h, 0, sizeof(h)) != sizeof(h)){
    printf("chkpt: write header failed\n");
    goto bad;
  }

  // 5. Write Trapframe (from snapshot)
  if(writei(ip, 0, (uint64)&tf_snapshot, sizeof(h), sizeof(struct trapframe)) != sizeof(struct trapframe)){
    printf("chkpt: write trapframe failed\n");
    goto bad;
  }

  // 6. Write Memory Pages (using snapshot pagetable)
  uint64 addr = 0;
  uint64 file_offset = sizeof(h) + sizeof(struct trapframe);
  
  while(addr < h.sz){
    uint64 chunk = PGSIZE;
    if (addr + chunk > h.sz)
        chunk = h.sz - addr;

    uint64 pa = walkaddr(victim_pagetable, addr);
    uint64 src_pa;

    if(pa == 0){
        // Page is unmapped (guard page or lazy alloc). Write zeros.
        src_pa = (uint64)zero_page;
    } else {
        // Page is valid, read from it.
        src_pa = pa;
    }

    if(writei(ip, 0, src_pa, file_offset, chunk) != chunk){
        printf("chkpt: write memory failed at addr 0x%lx\n", addr);
        goto bad;
    }

    addr += chunk;
    file_offset += chunk;
  }

  iunlockput(ip);
  end_op();
  kfree(zero_page);
  return 0;

bad:
  iunlockput(ip);
  end_op();
  kfree(zero_page);
  return -1;
}

uint64
sys_restore(void)
{
  char path[128];
  struct proc *p = myproc();
  struct chkpt_header h;
  struct inode *ip;

  if(argstr(0, path, 128) < 0)
    return -1;

  begin_op();
  if((ip = namei(path)) == 0){
    printf("restore: file not found\n");
    end_op();
    return -1;
  }
  ilock(ip);

  // 1. Read Header
  if(readi(ip, 0, (uint64)&h, 0, sizeof(h)) != sizeof(h)){
    printf("restore: read header failed\n");
    goto bad;
  }

  // 2. Clear Old Memory
  proc_freepagetable(p->pagetable, p->sz);
  p->sz = 0;
  
  // 3. Create New Page Table
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0)
    goto bad;

  // 4. Allocate New Memory (Aligned)
  uint64 sz_aligned = PGROUNDUP(h.sz);
  
  // FIX: Added PTE_X so the restored code is executable.
  // Without PTE_X, you get "usertrap(): unexpected scause 0xc" (Instruction Page Fault)
  if(uvmalloc(p->pagetable, 0, sz_aligned, PTE_W | PTE_X | PTE_R | PTE_U) == 0){
    printf("restore: uvmalloc failed\n");
    goto bad;
  }

  // 5. Read Trapframe SAFELY (Preserve Kernel Pointers)
  
  // Save CURRENT kernel pointers (belonging to this process/CPU)
  uint64 k_satp = p->trapframe->kernel_satp;
  uint64 k_sp   = p->trapframe->kernel_sp;
  uint64 k_trap = p->trapframe->kernel_trap;
  uint64 k_hartid = p->trapframe->kernel_hartid;

  // Overwrite trapframe from disk (User registers + Old Kernel Pointers)
  if(readi(ip, 0, (uint64)p->trapframe, sizeof(h), sizeof(struct trapframe)) != sizeof(struct trapframe)){
    printf("restore: read trapframe failed\n");
    goto bad;
  }

  // RESTORE correct kernel pointers for THIS process
  p->trapframe->kernel_satp = k_satp;
  p->trapframe->kernel_sp   = k_sp;
  p->trapframe->kernel_trap = k_trap;
  p->trapframe->kernel_hartid = k_hartid;

  // 6. Read Memory
  uint64 addr = 0;
  uint64 file_offset = sizeof(h) + sizeof(struct trapframe);
  
  while(addr < h.sz){
    uint64 chunk = PGSIZE;
    if (addr + chunk > h.sz)
        chunk = h.sz - addr;

    uint64 pa = walkaddr(p->pagetable, addr);
    if(pa == 0) {
        printf("restore: walkaddr failed at 0x%lx\n", addr);
        goto bad;
    }

    if(readi(ip, 0, pa, file_offset, chunk) != chunk){
        printf("restore: read memory failed at 0x%lx\n", addr);
        goto bad;
    }
    
    addr += chunk;
    file_offset += chunk;
  }

  iunlockput(ip);
  end_op();

  // 7. Critical State Fixes
  p->sz = h.sz;          // Exact size
  safestrcpy(p->name, h.name, sizeof(p->name)); // Restore Identity
  p->killed = 0;         // Clear kill flag
  p->trapframe->a0 = 0;  // Fake success return

  return 0;

bad:
  iunlockput(ip);
  end_op();
  return -1;
}