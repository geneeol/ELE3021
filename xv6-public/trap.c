#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

// project1
uint global_ticks;
int boosting_occured;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  // 시스템콜외 추가로 별도의 인터럽트 게이트 오픈
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  SETGATE(idt[T_PRAC2], 1, SEG_KCODE<<3, vectors[T_PRAC2], DPL_USER);
  SETGATE(idt[SCHED_LOCK], 1, SEG_KCODE<<3, vectors[SCHED_LOCK], DPL_USER);
  SETGATE(idt[SCHED_UNLOCK], 1, SEG_KCODE<<3, vectors[SCHED_UNLOCK], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed) //h 시스템콜 호출됐는데 상태가 killed면 종료
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER: //h 여기서 부스팅하기전에 yield부터 하는게 맞는듯
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      global_ticks++;
      if (global_ticks == 100)
      {
        global_ticks = 0;
        priority_boosting();
        boosting_occured = 1;
      }
      wakeup(&ticks); //h 타이머 인터럽트 발생시 ticks를 채널로 sleep하던 프로세스 깨움
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case T_PRAC2: // The line added
    cprintf("user interrupt 128 called\n");
    break;
  case SCHED_LOCK:
    cprintf("int 128 occured!\n");
    schedulerLock(PASSWORD);
    break ;
  case SCHED_UNLOCK:
    cprintf("int 129 occured!\n");
    schedulerUnlock(PASSWORD);
    break ;
  
  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER) // 실행됐는데 killed 상태면 종료
    exit();

  //h 확인결과 myproc가 널이거나, myproc 상태는 항상 running 같음
  //  즉 모든 프로세스가 sleep이어도 인터럽트는 발생해서 trap에 도달한다.
  //  그 외 일반적으로 프로세스사 running중에 인터럽트를 당해서 trap에 도달한다.
  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING
      && tf->trapno == T_IRQ0+IRQ_TIMER)
  {
    myproc()->used_ticks++; //h 현재 프로세스가 running이고 타이머 인터럽트를 받았다면, 한틱동안 큐에 머문 것
    yield();
  }

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
