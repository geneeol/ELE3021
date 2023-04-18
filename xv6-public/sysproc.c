#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void) //h 디버깅용 pid값 체크
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void) //h ticks 를 사용해슬립함수가 몇틱만큼 자는지 구현원리 확인 (슬립함수 인자)
{               //h ticks 오버플로우에 대한 에러처리가 방지돼 있지 않다
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){ //h n틱만큼 흐르길 기다림, 이때 busy wait이 아닌 진짜 sleep을 호출
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock); //h ticks를 chan인자로 하여 sleep한다
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void) //h 디버깅용 틱 체크 시스템콜
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// TODO: 시스템콜 래퍼함수 추가

int
sys_yield(void)
{
  yield();
  return (0);
}

int
sys_getLevel(void)
{
  return (getLevel());
}

int
sys_setPriority(void)
{
  int pid;
  int priority;

  if(argint(0, &pid) < 0)
    return (-1);
  if (argint(1, &priority) < 0)
    return (-1);
  return (setPriority(pid, priority));
}

int
sys_schedulerLock(void)
{
  int password;

  if (argint(0, &password) < 0)
    return (-1);
  schedulerLock(password);
  return (0);
}

int
sys_schedulerUnlock(void)
{
  int password;

  if (argint(0, &password) < 0)
    return (-1);
  schedulerUnlock(password);
  return (0);
}