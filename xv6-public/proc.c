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

t_queue mlfq[NMLFQ];

static const int mlfq_time_quantum[NMLFQ] = {4, 6, 8};

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

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void) //h userinit(첫번째 프로세스 생성)과 fork에서 호출
                //h 새로 생성된 프로세스의 정보를 첫번째로 초기화하는 곳
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
  p->qlev = 0;
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
  p->context->eip = (uint)forkret;

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
    cprintf("fork: queue_push failed");
  // else
  // {
  //   cprintf("np qlev, priority: %d, %d\n", np->qlev, np->priority);
  //   cprintf("push forked process: %d to L0 queue", np->pid);

  // }

  release(&ptable.lock);

  return pid;
}

// TODO:
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
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // TODO: 큐에서 pop 해야하는지 확인할 것! 안해도 상관은 없을 것 같은데 흠..
  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE; //h 현재 프로세스를 ZOMBIE 상태로 변경한다,
                           //h 따라서 해당 프로세스는 더 이상 스케줄링 되지 않는다.
                           //h 이후 부모프로세스의 wait호출을 통해 회수된다
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

  //cprintf("find_runnable_in_rr: begin: %d, end: %d\n", begin, end);
  for (int iter = begin; iter != end; iter = (iter + 1) % (NPROC + 1))
  {
    p = queue_front(q); // queue가 empty인 상황은 앞에서 걸러진다
    queue_pop(q);
    //cprintf("rr pid: %d, iter: %d\n", p->pid, iter);
    if (p->state == RUNNABLE)
      return p;
    queue_push_back(q, p); // sleeping 혹은 zombie 상태의 프로세스는 큐 맨뒤로 보낸다
                           // 정상적이라면 zombie 프로세스는 큐에 존재해선 안된다
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
  // if (flag >= 0)
  // {
  //   for (int i = 0; i < NPROC + 1; i++)
  //   {
  //     if (q->items[i] != 0)
  //       cprintf("q->items[%d], pid: %d, priority: %d\n", i, q->items[i]->pid, q->items[i]->priority);
  //   }
  //   cprintf("q front: %d, rear: %d\n", q->front, q->rear);
  //   // cprintf("\n\n");
  // }
  // cprintf("q size: %d, begin: %d, end: %d\n", queue_get_size(q), begin, end);
  // cprintf("find_runnable_in_fcfs: begin: %d, end: %d\n", begin, end);
  for (int iter = begin; iter != end; iter = (iter + 1) % (NPROC + 1))
  {
    tmp = q->items[iter]; // queue가 empty인 상황은 앞에서 걸러진다
    // if (flag > 30)
    // cprintf("fcfs tmp pid: %d, iter: %d\n", tmp->pid, iter);
    if (tmp->state == RUNNABLE && tmp->priority < lowest_priority)
    {
      // if (flag > 30)
        // cprintf("find candidate pid: %d\n\n", tmp->pid);
      lowest_priority = tmp->priority;
      p = tmp; // 큐를 전부 탐색하며 우선순위가 가장 낮은 프로세스를 찾는다
      // 만약 맨 앞에 있는 프로세스가 아니면, 
      // 맨 앞으로 보내기 위해 필요한 큐 회전 횟수를 기록한다
      rotate_cnt = len_from_begin(begin, iter); 
    }
  }
  // cprintf("after fcfs info\n\n");
  // if (flag >= 0)
  //   cprintf("rotate_cnt: %d\n", rotate_cnt);
  for (int i = 0; i < rotate_cnt; i++) // 선택한 프로세스를 큐 맨 앞으로 보낸다
  {
    tmp = queue_front(q);
    queue_pop(q);
    queue_push_back(q, tmp);
  }
  // if (!p)
  // {
  //   if (flag >=0)
  //     cprintf("can't find candidate\n");
  // }
  if (p) // 아 여기를 왜 !p로 했지..
  {
    // if (flag >= 0)
    //   cprintf("q front: %d is poped\n", queue_front(q)->pid);
    queue_pop(q);
    // if (flag >= 0)
    //   cprintf("after pop q front: %d, rear: %d\n\n", q->front, q->rear);
  }
  return (p);
}


// TODO:
//PAGEBREAK: 42
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
  
  // cprintf("scheduler starts\n");
  for(;;)
  {
    // Enable interrupts on this processor.
    sti();

    // TODO: 당연히 순회방식 변경
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    is_demoted = 0;

    //h 원래 for문 중괄호 시작
    for (int qlev = 0; qlev < 3; qlev++)
    {
      if (!queue_is_empty(&mlfq[qlev]))
      {
        // cprintf("find runnable: qlev:%d\n", qlev);
        if (qlev == 2)
          p = find_runnable_in_fcfs_priority(&mlfq[qlev]);
        else
          p = find_runnable_in_rr(&mlfq[qlev]);
        if (p) // 찾았으면 p는 널포인터가 아니다
          break;
      }
    }
    // procdump();
    // cprintf("p: %d\n", p->pid);
    if (!p) // 이런 상황 절대 발생하지 않음. 디버깅용 로그
    {
      // TODO: 아무것도 출력하지 않고 아래만 출력하면 여기서 러너블프로세스 없다고 뜸
      // cprintf("Fatal: no runnable process\n");
      release(&ptable.lock); //h 아 여기 락 해제를 안해서 ........ 
      continue ;
    }

    //h 스케쥴러에서 락한 것은 아마 선택된 프로세스가 lock을 릴리즈하는 것 같다 (릴리즈 후 다시 락)
    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;

    //cprintf("before swtch\n");
    swtch(&(c->scheduler), p->context);
    switchkvm(); //h 스케쥴러로 다시 컨텐스트 스위칭이 일어나면 이 부분부터 코드가 실행된다

    //cprintf("after switch\n");
    p->used_ticks++; // TODO: used_ticks 오버플로우 문제
    if (p->used_ticks >= mlfq_time_quantum[p->qlev]) // 직전에 실행된 프로세스의 타임퀀텀을 확인
    {
      p->used_ticks = 0;
      if (p->qlev == 2 && p->priority > 0)
          p->priority--;
      if (p->qlev < 2)
      {
        p->qlev++;
        is_demoted = 1;
      }
    }

    if (p->state != ZOMBIE) // 좀비 상태라면 레디큐에 존재할 필요가 없다. 슬립 상태는 다시 runnable로 바뀔 수 있으므로 큐 안에 보관한다
    {
      if (p->qlev == 2 && !is_demoted)
      {
        // cprintf("p: %d is pushed front\n", p->pid);
        queue_push_front(&mlfq[p->qlev], p); // 원래 l2큐에 있던 녀석이라면, 해당 큐의 맨앞으로 보낸다
        // queue_push_back(&mlfq[p->qlev], p);
      }
      else
      {
        queue_push_back(&mlfq[p->qlev], p);
        // if (p->qlev == 2)
          // cprintf("p: %d is firstly pushed back in L2\n", p->pid);
      }
    }
    // else
    // {
    //   cprintf("why is it zombie?\n");
    // }
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

//     // TODO: 당연히 순회방식 변경
//     // Loop over process table looking for process to run.
//     acquire(&ptable.lock);
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->state != RUNNABLE)
//         continue;

//       //h 선택된 프로세스가 lock을 해제하는 것 같다
//       // Switch to chosen process.  It is the process's job
//       // to release ptable.lock and then reacquire it
//       // before jumping back to us.
//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;

//       swtch(&(c->scheduler), p->context);
//       switchkvm(); 
//       //h 스케쥴러로 다시 컨텐스트 스위칭이 일어나면 이 부분부터 코드가 실행된다

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
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
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
sleep(void *chan, struct spinlock *lk)
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
  p->state = SLEEPING;

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

// TODO: 
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

  // TODO: 디버깅용 함수, 큐 순서대로 순회하게 변경
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
  for (int qlev = 0; qlev < 3; qlev++)
  {
    cprintf("qlev: %d\n", qlev);
    for (int i = 0; i < NPROC + 1; i++)
    {
      if (mlfq[qlev].items[i] != 0)
        cprintf("q->items[%d], pid: %d, priority: %d\n", i, mlfq[qlev].items[i]->pid, mlfq[qlev].items[i]->priority);
    }
    cprintf("q front: %d, rear: %d\n\n", mlfq[qlev].front, mlfq[qlev].rear);
  }
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

// void
// setPriority(int pid, int priority)
// {
//   acquire(&ptable.lock);
//   int pid;
//   int priority;
// }