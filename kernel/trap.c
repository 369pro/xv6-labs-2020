#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}
// 13: load page fault  15: read page fault
int mmap_handler(uint64 va, uint64 cause){
  struct proc *p = myproc();
  int i = 0;
  for(; i < NVMA; i++){
    // Q1 map的文件大小可能大于1页
    // if(p->vma[i].used && PGROUNDDOWN(va) == PGROUNDDOWN(p->vma[i].addr)){
    //   break;
    // }
    if(p->vma[i].used && p->vma[i].addr <= va && va <= p->vma[i].addr + p->vma[i].len-1){
      break;
    }
  }
  // 找不到空间映射
  if(i == NVMA){
    return -1;
  }
  // 处理权限问题
  // Q2 这么写就废了，不是通法
  // prot = p->vma[i].prot << 1;
  // 这里先赋值,这里是物理页的权限映射到虚拟页,不要设置PTE_V
  int prot = PTE_U;
  if(p->vma[i].prot & PROT_READ) prot |= PTE_R;
  if(p->vma[i].prot & PROT_WRITE) prot |= PTE_W;
  if(p->vma[i].prot & PROT_EXEC) prot |= PTE_X;
  
  struct file* f = p->vma[i].vfile;
  // Q3 如果read 引发page fault并且所映射的文件不能read直接返回错误
  if(cause == 13 && f->readable == 0){
    return -1;
  }// 同理
  if(cause == 15 && f->writable == 0){
    return -1;
  }
  void* pa = kalloc();
  // 分配内存失败
  if(pa == 0) {
    return -1;
  }
  memset(pa, 0, PGSIZE);
  
  ilock(f->ip);
  // Q4 内存中的偏移跟磁盘中的一样,并且要读取一个页面,需要page align
  // p->vma[i].offset假定为0
  int offset = PGROUNDDOWN(va - p->vma[i].addr);
  // Q5 读取到 [物理] 地址空间中
  int n = readi(f->ip, 0, (uint64)pa, offset, PGSIZE);
  if(n == 0){
    iunlock(f->ip);
    kfree(pa);
    return -1;
  }
  iunlock(f->ip);
  // Q6 不要在这外面判断呀!!!
  // if(n == 0){
  //   return -1;
  // }
  if(mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)pa,
  prot) != 0){
    kfree(pa);
    return -1;
  }
  return 0;
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  uint64 cause = r_scause();
  if(cause == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else if(cause == 13 || cause == 15){
    // handle page fault
    // stval寄存器包含无法翻译的地址
    // printf("cause=%d\n", cause);
    uint64 va = r_stval();
    // 此处fault_va处理与lazy实验一样
    if(PGROUNDUP(p->trapframe->sp)-1 < va && va < p->sz){
      if(mmap_handler(va, cause) != 0)
        p->killed = 1;
    }else{
      p->killed = 1;
    }
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", cause, p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

