#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 5
// #define NUM_THREAD 20

int status;
thread_t thread[NUM_THREAD];
int expected[NUM_THREAD];

void failed()
{
  printf(1, "Test failed!\n");
  // thread_exit(0);
  exit();
}

void *thread_basic(void *arg)
{
  int val = (int)arg;
  printf(1, "Thread %d start\n", val);
  if (val == 1) {
    sleep(200);
    status = 1;
  }
  printf(1, "Thread %d end\n", val);
  thread_exit(arg);
  return 0;
}

void *thread_fork(void *arg)
{
  int val = (int)arg;
  int pid;

  printf(1, "Thread %d start\n", val);
  pid = fork();
  if (pid < 0) {
    printf(1, "Fork error on thread %d\n", val);
    failed();
  }

  if (pid == 0) {
    printf(1, "Child of thread %d start\n", val);
    sleep(100);
    status = 3;
    printf(1, "Child of thread %d end\n", val);
    exit();
  }
  else {
    status = 2;
    if (wait() == -1) {
      printf(1, "Thread %d lost their child\n", val);
      failed();
    }
  }
  printf(1, "Thread %d end\n", val);
  thread_exit(arg);
  return 0;
}

int *ptr;

int ptr_arr[NUM_THREAD];

void *thread_sbrk(void *arg)
{ //h 여기가 eip 0xb5, 0xb9
  int val = (int)arg;
  // printf(1, "Thread %d start\n", val);
  // printf(1, "Thread tid: %d, arg: %d start\n", get_tid(), val);

  int i, j;
  int k;

  (void)k;
  
  k = 0;
  // bp_tracer("thread_sbrk"); // 이 다음이 072a

  if (val == 0) {
    // TODO: 1순위 고려사항. 간헐적으로 에러 발생하는데 왜일까?
    // TODO: free가 먼저 발생해서 아닐까 추측
    // 충분히 재우면 괜찮을듯? (충분히 재워도 발생했던듯..)
    ptr = (int *)malloc(65536);
    // 아래 sleep 없애고 free후에 sleep넣으면 확실히 트랩생길듯
    sleep(100);
    free(ptr);
    // sleep(1000);
    ptr = 0;
  }
  else {
    // 여기도 무한루프
    while (ptr == 0)
    {
      // if (k % 30 == 0)
      //   printf(1, "Thread %d tid: %d in first loop\n", val, get_tid());
      // k++;
      sleep(1);
    }

    // xv6에서는 free한 포인터에 값을 쓰는게 문제되지 않음...
    // 오직 ptr을 0으로 초기화 한 이후 값을 쓰는 행위만 문제되는듯
    for (i = 0; i < 16384; i++)
      ptr[i] = val; //h eip 0x10e, 0x106
  }

  k = 0;
  // 모든 쓰레드가 무한루프에 걸리고 메인 쓰레드가 계속 join에서 기다리는 상황 발생
  while (ptr != 0)
  {
    // if (k % 30 == 0)
    //   printf(1, "Thread %d tid %d in second loop\n", val, get_tid());
    // k++;
    sleep(1);
  }

  // 총 할당하는 메모리 65536 * 2000 * 5 = 655,360,000
  // 논리주소를 초과하긴 하지만 free랑 같이 해서 괜찮을것 같음.
  // free에서는 sbrk 호출하진 않음.
  for (i = 0; i < 2000; i++) {
    // 이유 알듯: xv6에서 malloc이 thread_safe하지 않기 때문... 
    // 여러 쓰레드가 동시에 sbrk를 호출한 상황.
    int *p = (int *)malloc(65536);
    if (p == 0)
      printf(1, "Thread: %d tid: %d malloc failed!", val, get_tid());
    ptr_arr[val] = (int)p;
    // if (i % 500 == 0)
    //   printf(1, "Thread: %d tid: %d p addr 0x%x\n", val, get_tid(), (int)p);
    for (j = 0; j < 16384; j++)
      p[j] = val; //h eip 0x160, 0x158
    // p[j]에 val을 할당했는데
    // 메모리 영역을 겹쳐서 할당받는게 문제인듯.
    for (j = 0; j < 16384; j++) {
      if (p[j] != val) {
        printf(1, "Thread %d tid: %d found %d, p_addr: 0x%x, j: %d\n\n", val, get_tid(), p[j], p, j);
        for (int i = 0; i < NUM_THREAD; i++)
          printf(1, "Thread %d p_addr: 0x%x\n", i, ptr_arr[i]);
        // printf(1, "Thread %d tid: %d found %d, j: %d\n", val, get_tid(), p[j], j);
        failed();
      }
    }
    free(p);
  }

  thread_exit(arg);
  return 0;
}
void create_all(int n, void *(*entry)(void *))
{
  int i;
  for (i = 0; i < n; i++) {
    if (thread_create(&thread[i], entry, (void *)i) != 0) {
      printf(1, "Error creating thread %d\n", i);
      failed();
    }
  }
}

void join_all(int n)
{
  int i, retval;
  for (i = 0; i < n; i++) {
    // 만약 thread[0]보다 thread[1]이 먼저 thread_exit하면?
    // thread[0]이 종료될 때까지 메인은 sleep
    if (thread_join(thread[i], (void **)&retval) != 0) {
      printf(1, "Error joining thread %d\n", i);
      failed();
    }
    if (retval != expected[i]) {
      printf(1, "Thread %d returned %d, but expected %d\n", i, retval, expected[i]);
      failed();
    }
  }
}

int main(int argc, char *argv[])
{
  int i;
  for (i = 0; i < NUM_THREAD; i++)
    expected[i] = i;

  printf(1, "Test 1: Basic test\n");
  create_all(2, thread_basic);
  sleep(100);
  printf(1, "Parent waiting for children...\n");
  join_all(2);
  if (status != 1) {
    printf(1, "Join returned before thread exit, or the address space is not properly shared\n");
    failed();
  }
  printf(1, "Test 1 passed\n\n");

  printf(1, "Test 2: Fork test\n");
  create_all(NUM_THREAD, thread_fork);
  join_all(NUM_THREAD);
  if (status != 2) {
    if (status == 3) {
      printf(1, "Child process referenced parent's memory\n");
    }
    else {
      printf(1, "Status expected 2, found %d\n", status);
    }
    failed();
  }
  printf(1, "Test 2 passed\n\n");

  printf(1, "Test 3: Sbrk test\n");
  create_all(NUM_THREAD, thread_sbrk);
  join_all(NUM_THREAD);
  printf(1, "Test 3 passed\n\n");

  printf(1, "All tests passed!\n");
  exit();
}
