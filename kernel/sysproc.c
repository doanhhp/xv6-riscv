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

static int
valid_usersz(uint64 sz)
{
  // User memory must stay below TRAPFRAME in xv6-riscv.
  if(sz > TRAPFRAME)
    return 0;
  // overflow-safe rounding
  uint64 a = PGROUNDUP(sz);
  if(a < sz)
    return 0;
  if(a > TRAPFRAME)
    return 0;
  return 1;
}

uint64
sys_checkpoint(void)
{
  int target_pid;
  char filename[64];
  struct proc *p, *tp = 0;
  struct inode *ip = 0;
  struct chkpt_header h;
  struct trapframe tf_copy;
  char *pagebuf = 0;

  argint(0, &target_pid);
  if(argstr(1, filename, sizeof(filename)) < 0)
    return -1;

  // Find target and snapshot (short critical section)
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == target_pid && p->state != UNUSED){
      if(p->state == ZOMBIE){
        release(&p->lock);
        return -1;
      }
      tp = p;

      h.pid = tp->pid;
      h.sz  = tp->sz;
      safestrcpy(h.name, tp->name, sizeof(h.name));

      if(!valid_usersz(h.sz)){
        release(&tp->lock);
        return -1;
      }

      // trapframe is kernel memory (kalloc'd) in standard xv6-riscv
      memmove(&tf_copy, tp->trapframe, sizeof(tf_copy));

      release(&tp->lock);
      break;
    }
    release(&p->lock);
  }

  if(tp == 0)
    return -1;

  pagebuf = (char*)kalloc();
  if(pagebuf == 0)
    return -1;

  // Create file + write header & trapframe
  begin_op();
  ip = create(filename, T_FILE, 0, 0);
  if(ip == 0){
    end_op();
    kfree(pagebuf);
    return -1;
  }
  // ip is LOCKED here.

  uint off = 0;

  if(writei(ip, 0, (uint64)&h, off, sizeof(h)) != sizeof(h))
    goto fail_locked_inode;
  off += sizeof(h);

  if(writei(ip, 0, (uint64)&tf_copy, off, sizeof(tf_copy)) != sizeof(tf_copy))
    goto fail_locked_inode;
  off += sizeof(tf_copy);

  iunlock(ip);
  end_op();

  // Dump memory: copy under proc lock -> write buffer to disk (no proc lock during I/O)
  uint64 addr = 0;
  while(addr < h.sz){
    uint chunk = PGSIZE;
    if(addr + chunk > h.sz)
      chunk = (uint)(h.sz - addr);

    acquire(&tp->lock);

    // Ensure target still valid
    if(tp->pid != target_pid || tp->state == UNUSED || tp->state == ZOMBIE){
      release(&tp->lock);
      goto fail_unlocked_inode;
    }

    uint64 pa = walkaddr(tp->pagetable, addr);
    if(pa == 0){
      memset(pagebuf, 0, chunk);
    } else {
      memmove(pagebuf, (void*)pa, chunk);
    }

    release(&tp->lock);

    begin_op();
    ilock(ip);
    int n = writei(ip, 0, (uint64)pagebuf, off, chunk);
    iunlock(ip);
    end_op();

    if(n != chunk)
      goto fail_unlocked_inode;

    off  += chunk;
    addr += chunk;
  }

  // Close inode ref
  begin_op();
  ilock(ip);
  iunlockput(ip);
  end_op();

  kfree(pagebuf);
  printf("chkpt: Saved process %d to %s\n", h.pid, filename);
  return 0;

fail_locked_inode:
  iunlockput(ip);
  end_op();
  kfree(pagebuf);
  return -1;

fail_unlocked_inode:
  if(ip){
    begin_op();
    ilock(ip);
    iunlockput(ip);
    end_op();
  }
  kfree(pagebuf);
  return -1;
}

uint64
sys_restore(void)
{
  char path[128];
  struct proc *p = myproc();
  struct chkpt_header h;
  struct inode *ip = 0;
  struct trapframe tf_disk;

  if(argstr(0, path, sizeof(path)) < 0)
    return -1;

  // Reads don't need a long transaction; just find + lock inode.
  ip = namei(path);
  if(ip == 0){
    printf("restore: file not found\n");
    return -1;
  }
  ilock(ip);

  // 1) Read header
  if(readi(ip, 0, (uint64)&h, 0, sizeof(h)) != sizeof(h)){
    printf("restore: read header failed\n");
    goto bad_locked;
  }
  if(!valid_usersz(h.sz)){
    printf("restore: invalid sz\n");
    goto bad_locked;
  }

  // 2) Read trapframe image into a temp (safer if restore later fails)
  uint64 off = sizeof(h);
  if(readi(ip, 0, (uint64)&tf_disk, off, sizeof(tf_disk)) != sizeof(tf_disk)){
    printf("restore: read trapframe failed\n");
    goto bad_locked;
  }
  off += sizeof(tf_disk);

  uint64 sz_aligned = PGROUNDUP(h.sz);

  // 3) Build new page table first (so failures don't destroy current process)
  pagetable_t oldpt = p->pagetable;
  uint64 oldsz = p->sz;

  pagetable_t newpt = proc_pagetable(p);
  if(newpt == 0){
    printf("restore: proc_pagetable failed\n");
    goto bad_locked;
  }

  // Allocate user memory (RWXU to avoid instruction faults)
  if(sz_aligned > 0){
    if(uvmalloc(newpt, 0, sz_aligned, PTE_R | PTE_W | PTE_X | PTE_U) == 0){
      printf("restore: uvmalloc failed\n");
      proc_freepagetable(newpt, 0);
      goto bad_locked;
    }
  }

  // 4) Read memory image directly into destination pages (fast path)
  uint64 addr = 0;
  while(addr < h.sz){
    uint chunk = PGSIZE;
    if(addr + chunk > h.sz)
      chunk = (uint)(h.sz - addr);

    uint64 pa = walkaddr(newpt, addr);
    if(pa == 0){
      printf("restore: walkaddr failed at 0x%lx\n", addr);
      proc_freepagetable(newpt, sz_aligned);
      goto bad_locked;
    }

    if(readi(ip, 0, pa, off, chunk) != chunk){
      printf("restore: read memory failed at 0x%lx\n", addr);
      proc_freepagetable(newpt, sz_aligned);
      goto bad_locked;
    }

    off  += chunk;
    addr += chunk;
  }

  // Done with file
  iunlockput(ip);
  ip = 0;

  // 5) Swap in new address space
  proc_freepagetable(oldpt, oldsz);
  p->pagetable = newpt;
  p->sz = h.sz;

  // 6) Install trapframe (preserve kernel-only fields)
  uint64 k_satp   = p->trapframe->kernel_satp;
  uint64 k_sp     = p->trapframe->kernel_sp;
  uint64 k_trap   = p->trapframe->kernel_trap;
  uint64 k_hartid = p->trapframe->kernel_hartid;

  memmove(p->trapframe, &tf_disk, sizeof(tf_disk));

  p->trapframe->kernel_satp   = k_satp;
  p->trapframe->kernel_sp     = k_sp;
  p->trapframe->kernel_trap   = k_trap;
  p->trapframe->kernel_hartid = k_hartid;

  // Final state fixes
  safestrcpy(p->name, h.name, sizeof(p->name));
  p->killed = 0;
  p->trapframe->a0 = 0;   // restore() returns 0

  return 0;

bad_locked:
  if(ip)
    iunlockput(ip);
  return -1;
}