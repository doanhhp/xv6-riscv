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

// helper ở vm.c (SUB-04 / SUB-05)
int vm_dump_memory(pagetable_t pagetable, uint64 sz,
                   struct inode *ip, uint *off);

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
// In kernel/sysproc.c

uint64
sys_checkpoint(void)
{
  int pid;
  char filename[128];
  struct proc *p;
  struct inode *ip;
  uint off = 0;
  int ret = -1;
  
  uint64 sz;            // Local copy of p->sz
  struct trapframe tf;  // Local copy of trapframe
  
  // Buffer for writing zeros (if a page is missing)
  char *zeropage = kalloc(); 
  if(zeropage == 0) {
    printf("sys_checkpoint: ERROR kalloc failed\n");
    return -1;
  }
  memset(zeropage, 0, PGSIZE);

  // 1. Fetch arguments
  argint(0, &pid);
  if(argstr(1, filename, 128) < 0) {
    printf("sys_checkpoint: ERROR failed to get filename\n");
    kfree(zeropage);
    return -1;
  }

  // 2. Find target process
  if((p = findproc(pid)) == 0) {
    printf("sys_checkpoint: ERROR: Process with PID %d not found.\n", pid);
    kfree(zeropage);
    return -1;
  }

  // 3. Copy state safely
  sz = p->sz;
  memmove(&tf, p->trapframe, sizeof(tf));
  
  // Release lock before file I/O to avoid deadlock
  release(&p->lock); 

  // 4. Create the file
  begin_op();
  if((ip = create(filename, T_FILE, 0, 0)) == 0){
    printf("sys_checkpoint: ERROR: failed to create file %s\n", filename);
    end_op();
    kfree(zeropage);
    return -1;
  }
  
  // 5. Write Header (Size)
  if(writei(ip, 0, (uint64)&sz, off, sizeof(sz)) != sizeof(sz)){
    goto bad;
  }
  off += sizeof(sz);

  // 6. Write Trapframe
  if(writei(ip, 0, (uint64)&tf, off, sizeof(tf)) != sizeof(tf)){
    goto bad;
  }
  off += sizeof(tf);
    if(vm_dump_memory(p->pagetable, sz, ip, &off) < 0){
    printf("sys_checkpoint: ERROR dump memory\n");
    goto bad;
  }

  printf("sys_checkpoint: checkpointed %d bytes to %s\n", off, filename);
  ret = 0;

bad:
  iunlockput(ip);
  end_op();
  return ret;
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


// In kernel/sysproc.c

uint64
sys_restore(void)
{
    char path[128];
    struct inode *ip;

    // (1) Fetch filename – GIỮ NGUYÊN
    if(argstr(0, path, sizeof(path)) < 0){
        printf("sys_restore: ERROR failed to get path\n");
        return -1;
    }

    // (2) Open file – GIỮ NGUYÊN
    begin_op();
    if((ip = namei(path)) == 0){
        printf("sys_restore: ERROR file '%s' not found\n", path);
        end_op();
        return -1;
    }
    ilock(ip);

    printf("sys_restore: DEBUG: Successfully opened '%s'\n", path);

  // ================================
  //        SUB-03 / SUB-04 / SUB-05
  // ================================

  uint off = 0;
  int ret = -1;
  uint64 sz;
  struct trapframe tf;

  struct proc *p = myproc();

  // --- SUB 03: đọc header ---
  if(readi(ip, 0, (uint64)&sz, off, sizeof(sz)) != sizeof(sz)){
    printf("sys_restore: ERROR reading size\n");
    goto bad;
  }
  off += sizeof(sz);

  if(readi(ip, 0, (uint64)&tf, off, sizeof(tf)) != sizeof(tf)){
    printf("sys_restore: ERROR reading trapframe\n");
    goto bad;
  }
  off += sizeof(tf);

  // --- SUB 04: giải phóng pagetable cũ và tạo pagetable mới ---

  // free old memory if exists
  if(p->sz > 0) {
    uvmfree(p->pagetable, p->sz);
    p->sz = 0;
  }

  pagetable_t newpt = uvmcreate();
  if(newpt == 0){
    printf("sys_restore: ERROR uvmcreate failed\n");
    goto bad;
  }

  // allocate new memory
if (uvmalloc(newpt, 0, sz, 1) != sz) {
    printf("sys_restore: ERROR uvmalloc failed\n");
    uvmfree(newpt, sz);
    goto bad;
  }

  p->pagetable = newpt;
  p->sz = sz;

  // --- SUB 05: đọc nội dung bộ nhớ từ file vào từng trang ---

  uint64 remaining = sz;
  for(uint64 va = 0; va < sz; va += PGSIZE){
    uint64 pa = walkaddr(p->pagetable, va);
    if(pa == 0){
      printf("sys_restore: ERROR walkaddr failed at va=0x%lx\n", va);
      goto bad;
    }

    int n = (remaining >= PGSIZE) ? PGSIZE : remaining;

    if(readi(ip, 0, (uint64)pa, off, n) != n){
      printf("sys_restore: ERROR reading memory chunk at va=0x%lx\n", va);
      goto bad;
    }

    if(n < PGSIZE){
      memset((void*)(pa + n), 0, PGSIZE - n);
    }

    off += n;
    remaining -= n;
  }

  // restore trapframe + fix return value
  *(p->trapframe) = tf;
  p->trapframe->a0 = 0;   // restore returns 0

  ret = 0;

bad:
  iunlockput(ip);
  end_op();
  return ret;
}
