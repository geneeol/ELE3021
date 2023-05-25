#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 10
#define NTEST 12

// Show race condition
int racingtest(void);

// Test basic usage of thread_create, thread_join, thread_exit
int basictest(void);
int jointest1(void);
int jointest2(void);

// Test whether a process can reuse the thread stack
int stresstest(void);

// Test what happen when some threads exit while the others are alive
int exittest1(void);
int exittest2(void);

// Test fork system call in multi-threaded environment
// int forktest(void);

// Test exec system call in multi-threaded environment
int exectest(void);

// Test what happen when threads requests sbrk concurrently
int sbrktest(void);

// Test what happen when threads kill itself
int killtest(void);

// Test pipe is worked in multi-threaded environment
int pipetest(void);

// Test sleep system call in multi-threaded environment
int sleeptest(void);

// Test behavior when we use set_cpu_share with thread
int stridetest1(void);
int stridetest2(void);

int gcnt;
int gpipe[2];

int (*testfunc[NTEST])(void) = {
  racingtest,
  basictest,
  jointest1,
  jointest2,
  stresstest,
  exittest1,
  exittest2,
  exectest,
  sbrktest,
  killtest,
  pipetest,
  sleeptest,
};
char *testname[NTEST] = {
  "racingtest", // 0
  "basictest", // 1
  "jointest1", // 2
  "jointest2", // 3
  "stresstest", // 4 TODO: 테스트 통과하기
  "exittest1", // 5 TODO: 테스트 통과하기
  "exittest2", // 6 TODO: 테스트 통과하기
  "exectest", // 7 TODO: 마이너 이슈 고치기
  "sbrktest", // 8
  "killtest", // 9
  "pipetest", // 10
  "sleeptest", // 11
};

int
main(int argc, char *argv[])
{
  int i;
  int ret;
  int pid;
  int start = 0;
  int end = NTEST-1;
  if (argc >= 2)
    start = atoi(argv[1]);
  if (argc >= 3)
    end = atoi(argv[2]);

  for (i = start; i <= end; i++){
    printf(1,"%d. %s start\n", i, testname[i]);
    if (pipe(gpipe) < 0){
      printf(1,"pipe panic\n");
      exit();
    }
    ret = 0;

    if ((pid = fork()) < 0){
      printf(1,"fork panic\n");
      exit();
    }
    if (pid == 0){
      close(gpipe[0]);
      ret = testfunc[i]();
      write(gpipe[1], (char*)&ret, sizeof(ret));
      close(gpipe[1]);
      exit();
    } else{
      close(gpipe[1]);
      if (wait() == -1 || read(gpipe[0], (char*)&ret, sizeof(ret)) == -1 || ret != 0){
        printf(1,"%d. %s panic\n", i, testname[i]);
        exit();
      }
      close(gpipe[0]);
    }
    printf(1,"%d. %s finish\n", i, testname[i]);
    sleep(100);
  }
  exit();
}

// ============================================================================
void nop(){ }

void*
racingthreadmain(void *arg)
{
  int tid = (int) arg;
  int i;
  int tmp;
  // sleep(100);
  for (i = 0; i < 10000000; i++){

    // #pragma GCC diagnostic push
    // #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    // if (i % 10000 == 0)
    // {
    //   printf(1,"pid: %d, tid: %d, tmp: %d, gcnt: %d\n", get_pid(), get_tid(), tmp, gcnt);
    //   sleep(1);
    // }
    // #pragma GCC diagnostic pop

    tmp = gcnt;
    tmp++;
	nop();
	// asm volatile("call %P0"::"i"(nop)); // "nop();
    // nop();
    gcnt = tmp;
  }
  thread_exit((void *)(tid+1));
  return (0);
}

int
racingtest(void)
{
  thread_t threads[NUM_THREAD];
  int i;
  void *retval;
  gcnt = 0;
  
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], racingthreadmain, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_join(threads[i], &retval) != 0 || (int)retval != i+1){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  printf(1,"%d\n", gcnt);
  return 0;
}

// ============================================================================
void*
basicthreadmain(void *arg)
{
  int tid = (int) arg;
  int i;
  for (i = 0; i < 100000000; i++){
    if (i % 20000000 == 0){
      printf(1, "%d", tid);
    }
  }
  thread_exit((void *)(tid+1));
  return (0);
}

int
basictest(void)
{
  thread_t threads[NUM_THREAD];
  int i;
  void *retval;
  
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], basicthreadmain, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_join(threads[i], &retval) != 0 || (int)retval != i+1){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  printf(1,"\n");
  return 0;
}

// ============================================================================

void*
jointhreadmain(void *arg)
{
  int val = (int)arg;
  sleep(200);
  printf(1, "thread_exit...\n");
  thread_exit((void *)(val*2));
  return (0);
}

int
jointest1(void)
{
  thread_t threads[NUM_THREAD];
  int i;
  void *retval;
  
  for (i = 1; i <= NUM_THREAD; i++){
    if (thread_create(&threads[i-1], jointhreadmain, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  printf(1, "thread_join!!!\n");
  for (i = 1; i <= NUM_THREAD; i++){
    if (thread_join(threads[i-1], &retval) != 0 || (int)retval != i * 2 ){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  printf(1,"\n");
  return 0;
}

int
jointest2(void)
{
  thread_t threads[NUM_THREAD];
  int i;
  void *retval;
  
  for (i = 1; i <= NUM_THREAD; i++){
    if (thread_create(&threads[i-1], jointhreadmain, (void*)(i)) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  sleep(500);
  printf(1, "thread_join!!!\n");
  for (i = 1; i <= NUM_THREAD; i++){
    if (thread_join(threads[i-1], &retval) != 0 || (int)retval != i * 2 ){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  printf(1,"\n");
  return 0;
}

// ============================================================================

void*
stressthreadmain(void *arg)
{
  thread_exit(0);
  return (0);
}

int
stresstest(void)
{
  const int nstress = 35000;
  thread_t threads[NUM_THREAD];
  int i, n;
  void *retval;

  for (n = 1; n <= nstress; n++){
    if (n % 1000 == 0)
      printf(1, "%d\n", n);
    for (i = 0; i < NUM_THREAD; i++){
      if (thread_create(&threads[i], stressthreadmain, (void*)i) != 0){
        printf(1, "panic at thread_create\n");
        return -1;
      }
    }
    for (i = 0; i < NUM_THREAD; i++){
      if (thread_join(threads[i], &retval) != 0){
        printf(1, "panic at thread_join\n");
        return -1;
      }
    }
  }
  printf(1, "\n");
  return 0;
}

// ============================================================================

void*
exitthreadmain(void *arg)
{
  int i;
  if ((int)arg == 1){
    while(1){
      printf(1, "thread_exit ...\n");
      for (i = 0; i < 5000000; i++);
    }
  } else if ((int)arg == 2){
    exit();
  }
  thread_exit(0);
  return (0);
}

// TODO: exit 테스트 체크, exit 디자인에 따라 결과가 달라질듯.
int
exittest1(void)
{
  thread_t threads[NUM_THREAD];
  int i;
  
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], exitthreadmain, (void*)1) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  sleep(1);
  return 0;
}

int
exittest2(void)
{
  thread_t threads[NUM_THREAD];
  int i;

  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], exitthreadmain, (void*)2) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  while(1);
  return 0;
}

// ============================================================================

void*
execthreadmain(void *arg)
{
  char *args[3] = {"echo", "echo is executed!", 0}; 
  sleep(1);
  exec("echo", args);

  printf(1, "panic at execthreadmain\n");
  exit();
}

// TODO: exec테스트 패닉 체크
int
exectest(void)
{
  thread_t threads[NUM_THREAD];
  int i;
  void *retval;

  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], execthreadmain, (void*)0) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_join(threads[i], &retval) != 0){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  printf(1, "panic at exectest\n");
  return 0;
}

// ============================================================================

void*
sbrkthreadmain(void *arg)
{
  int tid = (int)arg;
  char *oldbrk;
  char *end;
  char *c;
  oldbrk = sbrk(1000);
  end = oldbrk + 1000;

  for (c = oldbrk; c < end; c++){
    *c = tid+1;
  }
  sleep(1);

  for (c = oldbrk; c < end; c++){
    if (*c != tid+1){
      printf(1, "panic at sbrkthreadmain\n");
      exit();
    }
  }

  thread_exit(0);
  return (0);
}

int
sbrktest(void)
{
  thread_t threads[NUM_THREAD];
  int i;
  void *retval;

  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], sbrkthreadmain, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_join(threads[i], &retval) != 0){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }

  return 0;
}

// ============================================================================

void*
killthreadmain(void *arg)
{
  kill(getpid());
  while(1);
}

int
killtest(void)
{
  thread_t threads[NUM_THREAD];
  int i;
  void *retval;

  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], killthreadmain, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_join(threads[i], &retval) != 0){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  while(1);
  return 0;
}

// ============================================================================

void*
pipethreadmain(void *arg)
{
  int type = ((int*)arg)[0];
  int *fd = (int*)arg+1;
  int i;
  int input;
  for (i = -5; i <= 5; i++){
    if (type){
      read(fd[0], &input, sizeof(int));
      __sync_fetch_and_add(&gcnt, input);
      //gcnt += input;
    } else{
      write(fd[1], &i, sizeof(int));
    }
  }
  thread_exit(0);
  return (0);
}

int
pipetest(void)
{
  thread_t threads[NUM_THREAD];
  int arg[3];
  int fd[2];
  int i;
  void *retval;
  int pid;

  if (pipe(fd) < 0){
    printf(1, "panic at pipe in pipetest\n");
    return -1;
  }
  arg[1] = fd[0];
  arg[2] = fd[1];
  if ((pid = fork()) < 0){
      printf(1, "panic at fork in pipetest\n");
      return -1;
  } else if (pid == 0){
    close(fd[0]);
    arg[0] = 0;
    for (i = 0; i < NUM_THREAD; i++){
      if (thread_create(&threads[i], pipethreadmain, (void*)arg) != 0){
        printf(1, "panic at thread_create\n");
        return -1;
      }
    }
    for (i = 0; i < NUM_THREAD; i++){
      if (thread_join(threads[i], &retval) != 0){
        printf(1, "panic at thread_join\n");
        return -1;
      }
    }
    close(fd[1]);
    exit();
  } else{
    close(fd[1]);
    arg[0] = 1;
    gcnt = 0;
    for (i = 0; i < NUM_THREAD; i++){
      if (thread_create(&threads[i], pipethreadmain, (void*)arg) != 0){
        printf(1, "panic at thread_create\n");
        return -1;
      }
    }
    for (i = 0; i < NUM_THREAD; i++){
      if (thread_join(threads[i], &retval) != 0){
        printf(1, "panic at thread_join\n");
        return -1;
      }
    }
    close(fd[0]);
  }
  if (wait() == -1){
    printf(1, "panic at wait in pipetest\n");
    return -1;
  }
  if (gcnt != 0)
    printf(1,"panic at validation in pipetest : %d\n", gcnt);

  return 0;
}

// ============================================================================

// TODO: 슬립 테스트 체크
// 이것도 결국 exit시 모든 프로세스를 즉시 죽이느냐 마느냐의 문제
void*
sleepthreadmain(void *arg)
{
  printf(1, "tid %d before sleep\n", get_tid());
  // 원본 sleep(1000000);
  sleep(10000);
  printf(1, "tid %d is waked up\n", get_tid());
  thread_exit(0);
  return (0);
}

int
sleeptest(void)
{
  thread_t threads[NUM_THREAD];
  int i;

  // printf(1, "%s", __func__);
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], sleepthreadmain, (void*)i) != 0){
        printf(1, "panic at thread_create\n");
        return -1;
    }
  }
  sleep(10);
  return 0;
}

// ============================================================================

void*
stridethreadmain(void *arg)
{
  int *flag = (int*)arg;
  int t;
  while(*flag){
    while(*flag == 1){
      for (t = 0; t < 5; t++);
      __sync_fetch_and_add(&gcnt, 1);
    }
  }
  thread_exit(0);
  return (0);
}

// ============================================================================