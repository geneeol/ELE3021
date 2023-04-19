#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "mlfq.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

// project 1
t_queue mlfq[NMLFQ];
static const int mlfq_time_quantum[NMLFQ] = {4, 6, 8};

int sched_locked = 0;
int unlock_occured = 0;
int boosting_occured;

// static char *states2[] = {
//   [UNUSED]    "unused",
//   [EMBRYO]    "embryo",
//   [SLEEPING]  "sleep ",
//   [RUNNABLE]  "runble",
//   [RUNNING]   "run   ",
//   [ZOMBIE]    "zombie"
//   };


static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//h userinit(첫번째 프로세스 생성)과 fork에서 호출
// 새로 생성된 프로세스의 정보를 첫번째로 초기화하는 곳
//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) //h ptable에서 빈 슬롯에 프로세스 할당
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->priority = 3;
  p->qlev = L0;
  p->used_ticks = 0;
  // cprintf("allocproc: pid %d\n", p->pid);

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret; // eip는 pc 레지스터로 모든 자식은 forkret에서부터 시작한다

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p; //h initproc를 첫번째 프로세스인 init으로 초기화
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE; //h allocproc로 ptable에 프로세스 할당후 runnable로 바꿈으로서 스케쥴러가 실행가능하게 함

  // 첫 프로세스를 L0 큐에 넣는다
  // 락을 획득하고 큐에 push해야 인터럽트를 받지 않는다
  if (queue_push_back(&mlfq[L0], p) == -1) // 절대 실패하지 않지만 혹시나 해서..
    cprintf("userinit: queue_push failed");

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // cprintf("fork is called\n");

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid; //h 자식프로세스의 pid값을 반환하다.

  acquire(&ptable.lock);

  np->state = RUNNABLE; //h 생성한 프로세스를 이곳에서 RUNNABLE 상태로 변경
  // 새로운 자식은 항상 L0큐에 진입한다. 
  // 락이 걸린 상태에서 큐에 push해야 인터럽트에 방해받지 않는다
  if (queue_push_back(&mlfq[L0], np) == -1)
    cprintf("fork: queue_push failed\n");

  release(&ptable.lock);

  return pid;
}

//h 모든 프로세스는 종료전 exit을 명시적으로 호출해야하는 것 같다
// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  //h 고아 프로세스가 되지 않게끔, 현재 종료하려는 프로세스의 부모를 initproc로 바꾸어 준다
  // 만약 현재 exit을 호출한 프로세스를 부모프로세스로 하는 자식이 있다면 wait이 호출되기 전에 exit이 호출된 것이다
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  //h 큐에서 pop 해야하는지 확인할 것! 안해도 상관은 없을 것 같은데 흠..
  //  안해도 된다. 그냥 다시 큐에 집어넣지만 않으면 되는 것 
  //  Jump into the scheduler, never to return.
  curproc->state = ZOMBIE; //h 현재 프로세스를 ZOMBIE 상태로 변경한다,
                           //  따라서 해당 프로세스는 더 이상 스케줄링 되지 않는다.
                           //  이후 부모프로세스의 wait호출을 통해 회수된다
  sched(); //h scheduler로 컨텍스트 스위치가 되고 나면 두 번 다시 이 프로세스는 선택되지 않는다
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

struct proc *
find_runnable_in_rr(struct queue *q)
{
  struct proc *p;
  int begin;
  int end;

  begin = (q->front + 1) % (NPROC + 1);
  end = (q->rear + 1) % (NPROC + 1);

  for (int iter = begin; iter != end; iter = (iter + 1) % (NPROC + 1))
  {
    p = queue_front(q); // queue가 empty인 상황은 앞에서 걸러진다
    queue_pop(q);
    if (p->state == RUNNABLE)
      return p;
    // sleeping 혹은 zombie 상태의 프로세스는 큐 맨뒤로 보낸다
    // 단 정상적이라면 zombie 프로세스는 큐에 존재해선 안된다
    queue_push_back(q, p);
  }
  return 0;
}

struct proc *
find_runnable_in_fcfs_priority(struct queue *q)
{
  struct proc *p;
  struct proc *tmp;
  int lowest_priority;
  int begin;
  int end;
  int rotate_cnt; 
  static int  flag;

  p = 0;
  lowest_priority = 4;
  begin = (q->front + 1) % (NPROC + 1);
  end = (q->rear + 1) % (NPROC + 1);
  rotate_cnt = 0;
  flag++;
  for (int iter = begin; iter != end; iter = (iter + 1) % (NPROC + 1))
  {
    tmp = q->items[iter]; // queue가 empty인 상황은 앞에서 걸러진다
    // 큐를 전부 탐색하며 우선순위가 가장 낮은 프로세스를 찾는다
    // 해당 프로세스를 큐의 맨 앞으로 보내기 위해 필요한 큐 회전 횟수를 기록한다
    if (tmp->state == RUNNABLE && tmp->priority < lowest_priority)
    {
      lowest_priority = tmp->priority;
      p = tmp;
      rotate_cnt = dist_between_iters(begin, iter); 
    }
  }
  if (p)
  {
    // 선택한 프로세스를 큐 맨 앞으로 보낸다
    for (int i = 0; i < rotate_cnt; i++) 
    {
      tmp = queue_front(q);
      queue_pop(q);
      queue_push_back(q, tmp);
    }
    queue_pop(q);
  }
  return (p);
}

// All process are move to the L0 queue
// Every process's priority is set to 3
// Every process's time slice is set to 0
void
priority_boosting(void) //h 부스팅은 반드시 tickslock이 걸렸을 때 발생하기에 인터럽트 당하지 않는다
{
  int begin;
  int end;
  struct proc *poped;

  boosting_occured = 1;
  schedulerUnlock(PASSWORD);

  for (int qlev = L0; qlev <= L2; qlev++)
  {
    begin = (mlfq[qlev].front + 1) % (NPROC + 1);
    end = (mlfq[qlev].rear + 1) % (NPROC + 1);
    for (int iter = begin; iter != end; iter = (iter + 1) % (NPROC + 1))
    {
      mlfq[qlev].items[iter]->priority = 3;
      mlfq[qlev].items[iter]->used_ticks = 0;
      if (qlev > L0)
      {
        poped = queue_front(&mlfq[qlev]);
        poped->qlev = L0; // L1나 L2큐에 있는 프로세스의 큐레벨을 0으로 초기화한 후 L0큐에 넣는다
        queue_pop(&mlfq[qlev]);
        queue_push_back(&mlfq[L0], poped);
      }
    }
  }
}

// PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p = 0;
  struct cpu *c = mycpu();
  int is_demoted;
  c->proc = 0;
  
  for(;;)
  {
    // Enable interrupts on this processor.
    sti();
    //h 이 사이에서 인터럽트로 인한 부스팅이 발생할 수 있다

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    is_demoted = 0;
    // 현재 스케쥴러가 락돼있거나, 직전에 언락된 경우가 아닐 때만 큐를 순회하며 찾는다
    // 그 외 경우라면 이전에 스케쥴링됐던 프로세스 p를 다시 사용한다
    if (!sched_locked && !unlock_occured) 
    {
      // 이전에 사용한 p가 남아있을 수 있으므로 널로 리셋해준다
      p = 0;
      for (int qlev = L0; qlev <= L2; qlev++)
      {
        if (queue_is_empty(&mlfq[qlev]))
          continue ;
        if (qlev == L2)
          p = find_runnable_in_fcfs_priority(&mlfq[qlev]);
        else
          p = find_runnable_in_rr(&mlfq[qlev]);
        if (p) // 찾았으면 p는 널포인터가 아니다
          break;
      }
    }
    //h 모든 유저프로세스가 sleep이어도 스케쥴러는 돌아가기에 이 분기 발생
    if (!p)
    {
      release(&ptable.lock);
      continue ;
    }

    //h 스케쥴러에서 락한 것은 아마 선택된 프로세스가 lock을 릴리즈하는 것 같다
    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;

    swtch(&(c->scheduler), p->context);
    switchkvm(); //h 스케쥴러로 다시 컨텐스트 스위칭이 일어나면 이 부분부터 코드가 실행된다


    // 4가지 경우중 하나 1. 부스팅발생 2. 스케쥴러가 락됨 3. 방금 언락됨 4. 일반적인 상황
    // 경우를 1 / 2,3,4 로 나눔. 즉 if문과 if else if else 

    // 현재 p의 상태에 대해서!
    // 1. 만약 p가 running 중에 인터럽트를 당했다면 yield를 호출한 것임.
    // 따라서 yield에 의해 p 상태는 runnable로 변경
    // 2 좀비였다면 exit을 호출한 것이고,
    // 이는 인터럽트가 아닌 sched 호출에의해 스케쥴러로 바로 전환됨
    // 3 슬립 상태라면 다시 runnable로 바뀔 수 있으므로 큐에 넣어준다

    // 부스팅은 인터럽트가 발생햇을때만 가능, 즉 실행중이던 프로세스가 타임퀀텀 안에 안끝났을때만 발생
    if (boosting_occured)
    {
      boosting_occured = 0;
      p->priority = 3;
      p->qlev = L0;
      p->used_ticks = 0;
      // 부스팅이 발생한 경우에서도 두 가지 경우로 나뉜다 1. 스케쥴러 언락 발생 2. 일반적인 부스팅
      if (unlock_occured)
      {
        unlock_occured = 0;
        if (p->state != ZOMBIE) // zombie가 아님을 확인후 큐에 다시 집어넣는다 (방어적 코딩)
          queue_push_front(&mlfq[L0], p);  //h 부스팅에 의해 언락 발생했을시, 해당 프로세스는 다시 큐 맨앞으로 가야한다
      }
      else
      { 
        if (p->state != ZOMBIE)
          queue_push_back(&mlfq[L0], p); // 공평성을 위해 부스팅이 발생하면 L0맨 뒤에 넣는다
      }
      //h 아래 분기로 내려가지 않고 다시 continue를 통해 반복문 위로 올라간다 
      c->proc = 0;
      release(&ptable.lock);
      continue ;
    }

    if (sched_locked)
    {
      //h 정상적이라면 unlock을 호출후 exit할 때 큐에서 제거된다, 따라서 lock한 프로세스가 좀비면 절대 안된다
      if (p->state == ZOMBIE)
      {
        cprintf("pid: %d, sched is locked and zombie state\n", p->pid);
        sched_locked = 0;
      }
    }
    else if (unlock_occured) //h 직전에 unlock을 호출했다면
    {
      //h unlock함수는 락이 존재할 때만 unlock_occured 변수값을 1로 한다
      //  따라서 락 전에 언락을 호출하는 경우는 아무일도 발생하지 않는다.
      //  이는 부스팅에 의해 언락이 두번 호출되는 경우를 사전에 알 수 없기 때문에 이렇게 설계했다
      unlock_occured = 0;
      if (p->state != ZOMBIE) // unlock 발생했는데 종료되지 않았다면 mlfq 맨 앞에 삽입한다
      {
        p->priority = 3;
        p->qlev = L0;
        p->used_ticks = 0;
        queue_push_front(&mlfq[L0], p);
      }
    }
    else //h 일반적인 스케쥴러 동작 상황
    {
      if (p->used_ticks >= mlfq_time_quantum[p->qlev])
      {
        p->used_ticks = 0;
        if (p->qlev == L2 && p->priority > 0)
            p->priority--;
        if (p->qlev < L2)
        {
          p->qlev++;
          is_demoted = 1;
        }
      }
      if (p->state != ZOMBIE) 
      {
        if (p->qlev == L2 && !is_demoted) // 원래 l2큐에 있던 녀석만, 해당 큐의 맨앞으로 보낸다
          queue_push_front(&mlfq[p->qlev], p);
        else
          queue_push_back(&mlfq[p->qlev], p);
      }
    }
    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;

    release(&ptable.lock);
  }
}

// void
// scheduler(void)
// {
//   struct proc *p;
//   struct cpu *c = mycpu();
//   c->proc = 0;
  
//   for(;;){
//     // Enable interrupts on this processor.
//     sti();

//     // 당연히 순회방식 변경
//     // Loop over process table looking for process to run.
//     acquire(&ptable.lock);
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->state != RUNNABLE)
//         continue;

//       // 선택된 프로세스가 lock을 해제하는 것 같다
//       // Switch to chosen process.  It is the process's job
//       // to release ptable.lock and then reacquire it
//       // before jumping back to us.
//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;

//       swtch(&(c->scheduler), p->context);
//       switchkvm(); 
//       // 스케쥴러로 다시 컨텐스트 스위칭이 일어나면 이 부분부터 코드가 실행된다

//       // acquire(&tickslock);

//       // cprintf("switched to scheduler\n");
//       // cprintf("global_ticks: %d\n", global_ticks);
//       // cprintf("ticks: %d\n\n", ticks);
//       // if (global_ticks >= 100)
//       // {
//       //   global_ticks = 0;
//       //   priority_boosting();
//       // }

//       // release(&tickslock);

//       // Process is done running for now.
//       // It should have changed its p->state before coming back.
//       c->proc = 0;
//     }
//     release(&ptable.lock);

//   }
// }

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock //h 스케쥴러로 돌아가기 전에 락을 다시 잡아둔다
  myproc()->state = RUNNABLE;
  sched(); //h 이 부분에서 스케쥴러가 락을 잡아뒀다, 아랫줄이 실행되는 상황에서 스케쥴러는 락을 해제하지 않는다
  release(&ptable.lock); //h scheduler에서 걸어뒀던 락을 여기에서 풀어준다
}

//h 자식 프로세스는 항상 여기에서 시작한다.
// 자식 프로세스에서도 테이블 락을 해제해주어야 한다.
// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk) // 커널에서 프로세스를 재울 때 쓰는 함수, 유저랑 상관 없어보임
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING; //h 보통 ticks를 채널로해서 재우고 상태를 sleeping으로 바꾼다

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

void
mlfq_print(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int qlev;
  int iter;
  int begin, end;

  for(qlev = L0; qlev <= L2; qlev++)
  {
    cprintf("queue Level: %d, size: %d\n", qlev, queue_get_size(&mlfq[qlev]));
    cprintf("front:%d, rear:%d\n", mlfq[qlev].front, mlfq[qlev].rear);

    begin = (mlfq[qlev].front + 1) % (NPROC + 1);
    end = (mlfq[qlev].rear + 1) % (NPROC + 1);
    for(iter = begin; iter != end; iter = (iter + 1) % (NPROC + 1))
    {
      cprintf("items[%d], id: %d, %s %s\n",
              iter,
              mlfq[qlev].items[iter]->pid,
              states[mlfq[qlev].items[iter]->state],
              mlfq[qlev].items[iter]->name);
    }
    cprintf("\n");
  }
}

//h 프로세스 정보를 출력해주는 디버깅용 함수
//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
//h Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  cprintf(">======procdmp start======<\n");

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
  cprintf("\n*****mlfq info*****\n");
  mlfq_print();
  // for (int qlev = L0; qlev <= L2; qlev++)
  // {
  //   cprintf("qlev: %d\n", qlev);
  //   for (int i = 0; i < NPROC + 1; i++)
  //   {
  //     if (mlfq[qlev].items[i] != 0)
  //       cprintf("q->items[%d], pid: %d, priority: %d\n", i, mlfq[qlev].items[i]->pid, mlfq[qlev].items[i]->priority);
  //   }
  //   cprintf("q front: %d, rear: %d\n\n", mlfq[qlev].front, mlfq[qlev].rear);
  // }
  cprintf("\n>======procdmp finish======<\n");
  cprintf("\n\n");
}

/***** system calls for project1 *****/

int
getLevel(void)
{
	struct proc	*p;

	p = myproc();
  if (!p)
    return (-1); // proc 정보를 받을 수 없는 경우
  return (p->qlev);
}

void
setPriority(int pid, int priority)
{
  struct proc *p;
  int invalid_pid = 1;

  // priority가 0~3 사이가 아닌 경우에 대한 예외처리
  if (priority > 3)
    priority = 3;
  else if (priority < 0)
    priority = 0;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->priority = priority;
      invalid_pid = 0;
      break ;
    }
  }
  if (invalid_pid)
    cprintf("setPriority Error: invalid pid!\n");
  release(&ptable.lock);
}

void
schedulerLock(int password)
{
  struct proc *p;

  acquire(&ptable.lock); // 스케쥴락이 호출됐을 때, 작업이 종료되기 전 interrupt를 방지한다
  acquire(&tickslock); // 안해도 될 것 같긴한데 안전하게 추가..
  global_ticks = 0; // 부스팅 발생시 틱을 0으로 초기화 한다
  release(&tickslock);
  p = myproc();
  if (sched_locked) // 락을 두번 시도하면 패스워드가 틀렸을지라도 반드시 해제하고 exit해야 한다
  {
    sched_locked = 0;
    cprintf("Fatal: schedulerLock: Already locked!\n");
    cprintf("pid: %d, used_ticks: %d, qlev: %d\n\n", p->pid, p->used_ticks, p->qlev);
    release(&ptable.lock);
    while (wait() != -1) // 자식 회수
      ;
    exit();
  }
  if (password != PASSWORD)
  {
    sched_locked = 0;
    cprintf("Error: schedulerLock: invalid password!\n");
    cprintf("pid: %d, used_ticks: %d, qlev: %d\n\n", p->pid, p->used_ticks, p->qlev);
    release(&ptable.lock);
    while (wait() != -1) // 자식 회수
      ;
    exit();
  }
  sched_locked = 1;
  release(&ptable.lock);
}

void
schedulerUnlock(int password) 
{
  struct proc *p;

  acquire(&ptable.lock); // 스케쥴언락이 호출됐을 때, 작업이 종료되기 전 interrupt를 방지한다
  //h 암호가 일치하지 않더라도 강제종료를 해야하니, 락을 풀어주는게 타당함
  if (password != PASSWORD)
  {
    p = myproc();
    sched_locked = 0; 
    cprintf("Error: schedulerUnlock: invalid password!\n");
    cprintf("pid: %d, used_ticks: %d, qlev: %d\n\n", p->pid, p->used_ticks, p->qlev);
    release(&ptable.lock);
    while (wait() != -1) // 자식을 회수
      ;
    exit();
  }
  //h 락돼있지 않은데 언락이 호출되면? 그냥 무시한다
  //  부스팅에 의해 언락이 호출된 후, 사용자 언락이 재호출되는 경우는 충분히 발생가능하다
  //  이런 경우를 위해 언락이 두번 호출되면 아무일도 일어나지 않는다
  //  물론 사용자의 실수로 언락이 그냥 호출될 수도 있다. 이 경우도 그냥 아무처리 하지 않는다
  if (sched_locked)
  {
    sched_locked = 0;
    unlock_occured = 1;
  }
  release(&ptable.lock);
}