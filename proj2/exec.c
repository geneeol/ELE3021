#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;


extern void kill_and_retrieve_threads(struct proc *main);
extern void clean_proc_slot(struct proc *p);

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

void
kill_old_main_and_retrieve(struct proc *curproc, struct proc *old_main)
{
  int clean_old;

  clean_old = 0;
  for (; ;)
  {
    if (old_main->state == ZOMBIE)
    {
      clean_old = 1;
      clean_proc_slot(old_main);
    }
    else
    {
      old_main->killed = 1;
      wakeup1(old_main);
    }
    if (clean_old)
      return ;
    sleep(curproc, &ptable.lock);
  }
}

struct proc *
change_main_thread(struct proc *curproc, struct proc *old_main)
{
  old_main = curproc->main;
  old_main->main = curproc;
  old_main->is_main = 0;
  old_main->tid = 0; // 디버깅할 때 음수로 넣기.
  curproc->sz = old_main->sz;
  curproc->mem_limit = old_main->mem_limit;
  curproc->tid = 0;
  curproc->main = curproc;
  curproc->is_main = 1;
  curproc->retval = 0; // 메인 쓰레드이니 0으로 다시 초기화.
  return (old_main);
}

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();
  struct proc *old_main;

  acquire(&ptable.lock);

  // 현재 쓰레드가 메인 쓰레드가 아닌 경우 자원을 2단계로 나누어서 회수한다.
  // 1. 메인쓰레드를 현재 쓰레드로 바꾼후 old_main을 기준으로 서브 쓰레드를
  // 전부 kill하고 회수한다.
  // 2. 남아있는 old_main을 kill하고 회수한다.  
  if (!curproc->is_main)
  {
    old_main = change_main_thread(curproc, curproc->main);
    kill_and_retrieve_threads(old_main);
    kill_old_main_and_retrieve(curproc, old_main);
  }
  else
    kill_and_retrieve_threads(curproc);

  release(&ptable.lock);

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    // 코드, 데이터 영역에 해당하는 메모리 할당. 프로세스마다 해당 크기가 상이하다.
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  //h 가드용 페이지를 할당하는 부분
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  //h argument strings를 스택 탑에 한번에 하나씩 복사한다.
  //  이들에 대한 포인터를 ustack에 저장한다.
  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  //h 스택에 리턴주소, argc, argv 주소를 차례로 push
  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

int
exec2(char *path, char **argv, int stacksize)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();
  struct proc *old_main;

  if (stacksize < 1 || stacksize > 100)
    return (-1);

  acquire(&ptable.lock);

  // 현재 쓰레드가 메인 쓰레드가 아닌 경우 자원을 2단계로 나누어서 회수한다.
  // 1. 메인쓰레드를 현재 쓰레드로 바꾼후 이전 메인쓰레드를 기준으로 서브 쓰레드를
  // 전부 kill하고 회수한다.
  // 2. 남아있는 이전 메인 쓰레드를 kill하고 회수한다.  
  if (!curproc->is_main)
  {
    old_main = change_main_thread(curproc, curproc->main);
    kill_and_retrieve_threads(old_main);
    kill_old_main_and_retrieve(curproc, old_main);
  }
  else
    kill_and_retrieve_threads(curproc);

  release(&ptable.lock);

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  //h 스택 개수만큼 할당하도록 변경
  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + (stacksize + 1) * PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - (stacksize + 1) *PGSIZE));
  sp = sz;

  //h argument strings를 스택 탑에 한번에 하나씩 복사한다.
  //  이들에 대한 포인터를 ustack에 저장한다.
  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
