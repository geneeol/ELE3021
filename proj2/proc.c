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

int sched_locked = 0;
int unlock_occured = 0;

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
int nexttid = 1;
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
  p->mem_limit = UNLIMITED; //h 새로이 생성된 프로세스는 메모리 제한이 없다.
  p->tid = 0;
  p->is_main = 1;
  p->main = p;
  p->retval = 0;

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
  release(&ptable.lock);
}

//h sbrk를 호출하면 해당 함수를 통해 프로세스의 heap에 추가적인 메모리를 할당한다.
//  프로세스의 heap 사이즈와 페이지 테이블을 업데이트한다.
// 해당 함수는 sys_sbrk에서 호출되는데 함수 내부로 들어오기 전 항상 락을 잡고 진입한다.
// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  struct proc *main;

  // 락을 sys_sbrk 함수에서 걸고 들어옴.
  // acquire(&ptable.lock);

  main = curproc->main;
  sz = main->sz;
  //h mem_limit보다 늘렸을 때의 프로세스 메모리 사이즈가 작은지 체크
  //  uint + int 랑 int 자료형 비교하지만 문제 되진 않는듯.
  //  (애초에 limit이 int형 자료형이고 n도 int형) 

  // 쓰레드의 mem_limit이 아닌 main 쓰레드의 mem_limit과 비교한다
  if (sz + n > main->mem_limit)
    return -1;
  if(n > 0)
  {
    if((sz = allocuvm(main->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if(n < 0)
  {
    if((sz = deallocuvm(main->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  main->sz = sz;
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

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // 힙에 존재하는 데이터는 모든 쓰레드가 공유한다.
  // 따라서 메인 쓰레드의 pgdir과 sz을 이용하는 것이 옳다. (pgdir은 어차피 공유)
  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->main->pgdir, curproc->main->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  // 메인 쓰레드의 pgdir을 통째로 복사했으므로 sz값도 main의 값으로 복사한다.
  np->sz = curproc->main->sz;

  // fork를 한 쓰레드가 wait을 통해 자기 스스로 자식을 회수하는 건 자연스럽다 
  // 즉 쓰레드가 다른 프로세스의 부모가 될 수 있다.
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

  release(&ptable.lock);
  return pid;
}

void
clean_proc_slot(struct proc *p)
{
  kfree(p->kstack);
  p->kstack = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->killed = 0;
  p->state = UNUSED;
  // proj2 맴버에 대한 삭제 부분
  p->mem_limit = 0;
  p->tid = 0;
  p->main = 0;
  p->is_main = 0;
  p->retval = 0;
}

void
kill_and_wait_threads(struct proc *curproc)
{
  struct proc *p;
  int nthread;

  acquire(&ptable.lock);
  for (;;)
  {
    nthread = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->main != curproc || p->is_main)
        continue ;
      if (p->state == ZOMBIE)
        clean_proc_slot(p);
      else
      {
        p->killed = 1;
        nthread++;
        // ptable락을 잡고 있으므로 wakeup1 사용
        wakeup1(p);
      }
    }
    if (nthread == 0)
    {
      release(&ptable.lock);
      return ;
    }
    sleep(curproc, &ptable.lock);
  }
}

// TODO: 메인 쓰레드가 아닌 쓰레드가 exit을 호출했을 때 자원 회수.
// TODO: 1. 만약 쓰레드가 fork를 했고 해당 프로세스가 thread_exit하는 상황
//       2. 메인 쓰레드가 exit하고 서브 쓰레드가 fork를 한 상황
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


  // TODO: 이 부분은 조교님 답변 받고 결정하자.
  // kill에 의해 쓰레드가 exit을 호출하면 thread_exit을 호출하게 수정 
  // (이러한 수정은 kill에 의해 프로세스가 종료되는 경우가 있을 수 있기 때문)
  // kill에 의해 쓰레드가 exit을 콜하는 경우가 생길 수 있다. (현재 구현에서는)
  // 이 부분을 바꾸려면 trap.c에서 thread_exit을 호출하게 하면 됨.
  // thread_exit과 exit은 wakeup 해주는 프로세스 외에는 차이가 없다.
  // 또 exit의 경우 메인쓰레드라면 서브 스레드를 전부 죽이고 exit한다.
  // 수정방향: kill pid 때리고 trap.c 수정해두기. 
  if (!curproc->is_main)
    thread_exit(0);
  
  // 메인 쓰레드는 서브 쓰레드가 전부 종료된 후 exit할 수 있다.
  if (curproc->is_main)
    kill_and_wait_threads(curproc);
  
  // Close all open files
  for(fd = 0; fd < NOFILE; fd++)
  {
    if(curproc->ofile[fd])
    {
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
  //h 이미 ptable.lock을 잡았으므로 바로 wakeup1 호출
  // TODO: 만약 쓰레드가 exit할 수 있다면 이 부분 수정 필요
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  //h 고아 프로세스가 되지 않게끔, 현재 종료하려는 프로세스의 부모를 initproc로 바꾸어 준다
  // 만약 현재 exit을 호출한 프로세스를 부모프로세스로 하는 자식이 있다면 wait이 호출되기 전에 exit이 호출된 것이다
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->parent == curproc)
    {
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
  for(;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if(p->parent != curproc)
        continue;
      havekids = 1;
      // 현재 문제가 부모 - 자식 관계에서 자식 쓰레드 생성시 부모가 자식 쓰레드를 회수해버림.
      // 쓰레드의 부모를 현재 메인 쓰레드의 부모로 지정하되, 
      // 쓰레드는 wait을 통해 회수되지 못하도록 p->is_main 조건문에 추가.
      if(p->state == ZOMBIE && p->is_main)
      {
        // Found one.
        pid = p->pid;
        clean_proc_slot(p);
        freevm(p->pgdir);
        release(&ptable.lock);
        return pid;
      }
    }
    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }
    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

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
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

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
    //h sleeplock인 경우 lk가 ptable.lock과 다름. 이 경우 sleep하기 전에 release 필요
    //h release가 필요한 이유. 1. 데드락 방지 2. 비지웨잇 막기 위해?
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING; //h 보통 ticks를 채널로해서 재우고 상태를 sleeping으로 바꾼다

  sched();
  //h sleep하던 프로세스는 여기서 깨어날 수도 있음. 이 때 killed 상태일 수도.

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// 해당 함수는 호출 전에 ptable.lock이 잡혀있다.
// pid에 해당하는 모든 쓰레드를 깨운다. (join을 호출한 쓰레드가 누구인지 모르므로)
void
wakeup_pid1(int pid)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid == pid && p->state == SLEEPING)
      p->state = RUNNABLE;
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
  {
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
  }
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
  int invalid_pid;

  acquire(&ptable.lock);
  invalid_pid = 1;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    // 메인쓰레드 및 쓰레드 전부 kill 플래그 설정
    if(p->pid == pid)
    {
      invalid_pid = 0;
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
    }
  }
  release(&ptable.lock);
  if(invalid_pid)
    return -1;
  return (0);
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
    cprintf("pid: %d, tid: %d %s %s", p->pid, p->tid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
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

/***** system calls for project2 *****/


// 쓰레드끼리는 sz값이 동일하느냐? 만약 동일하지 않으면 특정 쓰레드에서
// limit값 설정에 실패하고, 다른 쓰레드에선 성공하는 경우 고려해야함
// 디자인: main 쓰레드의 sz값만 의미있다고 판단.
// sbrk를 호출시에도 메인쓰레드의 sz값을 이용함. 메모리 관련은 전부 메인쓰레드가 담당.
// 따라서 메인 쓰레드의 mem_limit값만 관리하자
int
setmemorylimit(int pid, int limit)
{
  struct proc *p;

  if (limit < 0)
    return (-1);
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid && p->is_main)
    {
      if (p->sz > limit)
      {
        release(&ptable.lock);
        return (-1);
      }
      if (limit == 0)
        p->mem_limit = UNLIMITED;
      else
        p->mem_limit = limit;
      release(&ptable.lock);
      return (0);
    }
  }
  release(&ptable.lock);
  return (-1);
}

// print process's information
// it it include process's name, pid, the number of stack page, allocated memory size, limit of memory size
// Also, it should consider its thread information
int
plist(void)
{
  struct proc *p;
  
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue ;
    if (p->state == ZOMBIE)
    {
      cprintf("ZOMBIE detect!: pid: %d,", p->pid);
      if (!p->is_main)
        cprintf("it's thread: tid: %d\n", p->tid);
      else
        cprintf("it's main process\n");
    }
    if (p->is_main)
      cprintf("name: %s, pid: %d, page: %d, memory size: 0x%x, limit: 0x%x\n", \
                      p->name, p->pid, p->sz / PGSIZE, p->sz, p->mem_limit);
  }
  release(&ptable.lock);
  return (0);
}

// static struct thread*
// allocthread(struct proc *p)
// {
//   struct thread *t;
//   char *sp;

//   acquire(&ptable.lock);
//   //h 이부분에서 ptable락은 필요 없을듯?
//   // 만약 goto found로 갔는데 그 사이 다른쓰레드가 할당해버리면? 
//   // 락 필요하네
//   // allocthread는 alloproc와 무관하게 호출될 수 있으므로 함수 내부에서 그냥 락을 잡자
//   for(t = p->threads; t < &p->threads[NTHREAD]; t++) //h ptable에서 빈 슬롯에 프로세스 할당
//   {
//     if(t->state == UNUSED)
//       goto found;
//   }
//   release(&ptable.lock);
//   return 0;

// found:
//   t->state = EMBRYO;
//   t->tid = nexttid++;
//   t->main_proc = p;

//   release(&ptable.lock);

//   // Allocate kernel stack.
//   if((t->kstack = kalloc()) == 0){
//     t->state = UNUSED;
//     return 0;
//   }
//   sp = t->kstack + KSTACKSIZE;

//   // Leave room for trap frame.
//   sp -= sizeof *t->tf;
//   t->tf = (struct trapframe*)sp;

//   // Set up new context to start executing at forkret,
//   // which returns to trapret.
//   sp -= 4;
//   *(uint*)sp = (uint)trapret;

//   sp -= sizeof *t->context;
//   t->context = (struct context*)sp;
//   memset(t->context, 0, sizeof *t->context);
//   t->context->eip = (uint)forkret; // eip는 pc 레지스터로 모든 자식은 forkret에서부터 시작한다
//   p->n_thread++;

//   return t;
// }


// int
// thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg)
// {
//   int i, pid;
//   uint sz, sp;
//   struct thread *nt;
//   struct proc *curproc = myproc();

//   // Allocate process.
//   if((nt = allocthread(curproc)) == 0){
//     return -1;
//   }

//   sz = curproc->sz;
//   if (sz = allocuvm(curproc->pgdir, sz, sz + 2 * PGSIZE) == 0)
//   {
//     kfree(nt->kstack);
//     nt->kstack = 0;
//     nt->state = UNUSED;
//     return (-1);
//   }

//   curproc->sz = sz;
//   sp = sz;

//   sp -= 4;
//   *(uint *)sp = (uint)arg;
//   sp -= 4;
//   *(uint *)sp = (uint)0xffffffff;
//   nt->tf->eip = (uint)start_routine;
//   nt->tf->esp = sp;
//   *thread = nt->tid;

//   acquire(&ptable.lock);

//   t->state = RUNNABLE;

//   release(&ptable.lock);
//   return 0;
// }

// void
// thread_start(void *(*start_routine)(void *), void *arg)
// {
//   void *retval;

//   cprintf("thread_start\n");
//   retval = start_routine(arg);
//   thread_exit(retval);
// }

// TODO: 함수 나누기
// fork함수와 exec함수에서 필요한 부분을 가져와서 구성 
int
thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg)
{
  int i;
  uint sz;
  uint sp;
  struct proc *curproc;
  struct proc *main;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  nextpid--;
  curproc = myproc();

  // TODO: memlimit 정보 메인 쓰레드와 동일하게
  main = curproc->main;
  np->tid = nexttid++;
  np->main = main;
  np->is_main = 0;
  np->pid = main->pid;

  // 메인 쓰레드와 pgdir을 공유하기 위해 락이 필요함
  // 락을 걸지 않으면 pgdir에 대한 레이스 컨디션 발생 가능
  acquire(&ptable.lock);

  sz = main->sz;
  np->pgdir = main->pgdir;
  // 쓰레드는 메인 쓰레드와 디렉토리(첫번째 페이지 테이블)을 공유한다.
  // 해당 테이블 공간에 해당하는 유저 스택을 할당해서 쓰레드가 사용하게끔 한다.
  if ((sz = allocuvm(main->pgdir, sz, sz + 2 * PGSIZE)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return (-1);
  }
  // 유저 스택 밑에 가드 페이지 추가.
  clearpteu(main->pgdir, (char*)(sz - 2*PGSIZE));

  release(&ptable.lock);

  // 메인쓰레드의 메모리 사용량을 업데이트
  *thread = np->tid;
  main->sz = sz;
  np->sz = sz;
  np->parent = main->parent;
  *np->tf = *main->tf;

  // calling convention에 맞게 스택에 인자, 리턴주소를 차례로 push
  // eip 레지스터에 start_routine주소를 할당한다. (pc 레지스터) 
  // 쓰레드는 유저 영역 코드를 실행할 start_routine부터 실행된다.
  sp = sz;
  sp -= 4;
  *(uint *)sp = (uint)arg;
  sp -= 4;
  *(uint *)sp = (uint)0xffffffff;
  np->tf->eip = (uint)start_routine;
  np->tf->esp = sp;

  // TODO: 아래 부분 필요한지 확인.
  // 쓰레드끼리는 open file 리스트를 공유한다. 
  for(i = 0; i < NOFILE; i++)
  {
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  }
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  acquire(&ptable.lock);

  np->state = RUNNABLE; //h 생성한 프로세스를 이곳에서 RUNNABLE 상태로 변경

  release(&ptable.lock);
  return (0);
}

// TODO: 나중에 고려할 사항. main이 exit할 때 자원회수를 누가할 것인가.
//       메인 쓰레드도 thread_exit을 호출할 수 있음.
//       실제 pthread에서 다른 쓰레드는 계속 실행된다고 함.
//       현재 구현에서는 exit을 호출하도록 디자인.
// 테스트케이스: 메인 쓰레드 thread_exit, 다른 쓰레드에서 thread_join 하는 경우
void
thread_exit(void *retval)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // TODO: 일단 약식으로 구현하자. main 쓰레드가 thread_exit을 호출하면
  // 모든 프로세스를 종료한다.
  if (curproc->is_main)
    exit();

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

  // 스케쥴러로 돌아가기 전에 락을 잡는다.
  acquire(&ptable.lock);

  curproc->retval = retval;

  // Parent might be sleeping in wait().
  // 여기 조건문 필요 없을듯.
  // if (!curproc->is_main)
  //   wakeup1(curproc->main);
  wakeup_pid1(curproc->pid);

  // 현재 종료되는 쓰레드가 누군가의 부모일 수 있다.
  // 따라서 해당 프로세스(쓰레드)의 자식을 initproc에 인계한다.
  // 이로써 고아프로세스를 initproc가 회수한다.
  // 아래 for문 주석처리 하면 쓰레드에서 fork한 자식들이 고아가 됨.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->parent == curproc)
    {
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  //  Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched(); //h scheduler로 컨텍스트 스위치가 되고 나면 두 번 다시 이 프로세스는 선택되지 않는다
  panic("zombie exit");
}

// int
// thread_join(thread_t thread, void **retval)
// {
//   struct proc *p;
//   struct thread *t;
//   int havekids, pid;
//   struct proc *curproc = myproc();
  
//   acquire(&ptable.lock);
//   for(;;){
//     // Scan through table looking for exited children.
//     havekids = 0;
//     for(t = curproc->threads; t < &curproc->threads[NPROC]; t++)
//     {
//       if( t->state == ZOMBIE)
//       {
//         // Found one.
//         *retval = t->retval;
//         kfree(t->kstack);
//         t->kstack = 0;
//         t->state = UNUSED;
//         release(&ptable.lock);
//         return (0);
//       }
//     }

//     // No point waiting if we don't have any children.
//     if(!havekids || curproc->killed){
//       release(&ptable.lock);
//       return -1;
//     }

//     // Wait for children to exit.  (See wakeup1 call in proc_exit.)
//     sleep(curproc, &ptable.lock);  //DOC: wait-sleep
//   }
// }

// TODO: '같은 쓰레드 그룹끼리만 회수할 수 있게 설정'
// TODO: 데드락이 감지된 경우 에러 처리 (이 부분은 구현 못할듯)
int
thread_join(thread_t thread, void **retval)
{
  struct proc *p;
  int foundthread;
  struct proc *curproc = myproc();
  
  // thread id가 자기 자신인 경우, tid가 0인 경우 예외처리
  if (curproc->tid == thread || thread == 0)
    return (-1);
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    foundthread = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->tid != thread)
        continue;
      // 같은 쓰레드 그룹인지 확인
      if (p->main != curproc->main)
      {
        release(&ptable.lock);
        return (-1);
      }
      foundthread = 1;
      if(p->state == ZOMBIE)
      {
        // Found one.
        if (retval)
          *retval = p->retval;
        // TODO: dealloc이용해서 스택 회수하는 로직도 고려해볼 것.
        clean_proc_slot(p);
        release(&ptable.lock);
        return (0);
      }
    }
    // No point waiting if we don't have any children.
    // 현재 프로세스가 이미 kill 됐으면 쓰레드 그룹이 전부 kill됐을 거임.
    // kill 되었더라도 루프를 한번더 돌고 자원 회수가능하면 회수
    if(!foundthread || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }
    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
    //h 만약 curproc가 sleep중에 kill됐으면 여기서 깨어날 수 있음
    //  killed 상태일지라도 루프를 한번 더 돌며 타깃이 좀비면 회수
  }
}

int
retrieve_sub_threads(int pid)
{
  struct proc *p;
  int fd;

  // TODO: 여기서 락잡으면 왜 터지지?
  // cprintf("starting retrieve_sub_threads: pid = %d\n", pid);
  // if (holding(&ptable.lock))
  //   cprintf("lock is already hold!\n");
  acquire(&ptable.lock);
  // cprintf("after acquire\n");
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid && !p->is_main)
    {
      if (p->state != ZOMBIE)
      {
        for(fd = 0; fd < NOFILE; fd++)
        {
          if(p->ofile[fd])
          {
            fileclose(p->ofile[fd]);
            p->ofile[fd] = 0;
          }
        }
        begin_op();
        iput(p->cwd);
        end_op();
        p->cwd = 0;
      }
      clean_proc_slot(p);
    }
  }
  // cprintf("retrieve_sub_threads is done: pid = %d\n", pid);
  release(&ptable.lock);
  return (0);
}

int
get_pid(void)
{
  struct proc	*p;

	p = myproc();
  if (!p)
    return (-1); // proc 정보를 받을 수 없는 경우
  return (p->pid);
}

int
get_tid(void)
{
  struct proc *p;

  p = myproc();
  if (!p)
    return (-1);
  return (p->tid);
}
