#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

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
  // 락을 걸어서 페이지 테이블에 대한 레이스 컨디션 방지
  acquire(&ptable.lock);
  // sbrk 리턴값은 이전 메모리 사이즈 (top of heap)
  addr = myproc()->main->sz;
  if(growproc(n) < 0)
  {
    release(&ptable.lock);
    return -1;
  }
  release(&ptable.lock);
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
  setPriority(pid, priority);
  return (0);
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

int
sys_bp_tracer(void)
{
  char *msg;

  if (argstr(0, &msg) < 0)
    return (-1);
  return (0);
}

int
sys_mutex_init(void)
{
  struct spinlock *lock;
  char  *name;

  // TODO: sizeof struct spinlock인지 lockd인지 체크할것
  if (argptr(0, (char **)&lock, sizeof(struct spinlock)) < 0)
    return (-1);
  if (argstr(1, &name) < 0)
    return (-1);
  initlock(lock, name);
  cprintf("sys_mutex_init: %s, %p\n", lock->name, lock);
  return (0);
}

int
sys_mutex_lock(void)
{
  struct spinlock *lock;

  if (argptr(0, (char **)&lock, sizeof(struct spinlock)) < 0)
    return (-1);
  acquire(lock);
  cprintf("sys_mutex_lock: %s, %p\n", lock->name, lock);
  return (0);
}

int
sys_mutex_unlock(void)
{
  struct spinlock *lock;
  if (argptr(0, (char **)&lock, sizeof(struct spinlock)) < 0)
    return (-1);
  release(lock);
  cprintf("sys_mutex_unlock: %s, %p\n", lock->name, lock);
  return (0);
}

/***** proj2 *****/

int
sys_setmemorylimit(void)
{
  int pid;
  int limit;

  if (argint(0, &pid) < 0)
    return (-1);
  if (argint(1, &limit) < 0)
    return (-1);
  return (setmemorylimit(pid, limit));
}

int
sys_plist(void)
{
  return (plist());
}

int
sys_thread_create(void)
{
  thread_t *thread;
  void *(*start_routine)(void *);
  void *arg;

  if (argptr(0, (char **)&thread, sizeof(thread)) < 0)
    return (-1);
  if (argptr(1, (char **)&start_routine, sizeof(start_routine)) < 0)
    return (-1);
  if (argptr(2, (char **)&arg, sizeof(arg)) < 0)
    return (-1);
  return (thread_create(thread, start_routine, arg));
}

int
sys_thread_exit(void)
{
  void *retval;

  if (argptr(0, (char **)&retval, sizeof(retval)) < 0)
    return (-1);
  thread_exit(retval);
  return (0);
}

int
sys_thread_join(void)
{
  thread_t thread;
  void **retval;
  if (argint(0, &thread) < 0)
    return (-1);
  if (argptr(1, (char **)&retval, sizeof(retval)) < 0)
    return (-1);
  return (thread_join(thread, retval));
}

int
sys_get_pid(void)
{
  return (get_pid());
}

int
sys_get_tid(void)
{
  return (get_tid());
}