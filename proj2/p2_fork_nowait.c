#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 5

int status;
thread_t thread[NUM_THREAD];
int expected[NUM_THREAD];

void failed()
{
  printf(1, "Test failed!\n");
  exit();
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
  else
  {
    // 원본 코드에서 wait하는 부분 삭제
    status = 2;
  }
  printf(1, "Thread %d end\n", val);
  thread_exit(arg);
  return 0;
}

int *ptr;

void create_all(int n, void *(*entry)(void *))
{
  int i;
  for (i = 0; i < n; i++) {
    if (thread_create(&thread[i], entry, (void *)i) != 0) {
      printf(1, "Error creating thread %d\n", i);
      failed();
    }
    // 여기 sleep 추가시 첫번째 스레드가 좀비가 됐다가 회수되는듯..?
    // sleep(1000);
  }
}

void join_all(int n)
{
  int i, retval;
  for (i = 0; i < n; i++) {
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

// 쓰레드에서 fork후 쓰레드에서 wait으로 회수하지 않음.
int main(int argc, char *argv[])
{
  int i;
  for (i = 0; i < NUM_THREAD; i++)
    expected[i] = i;

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
  exit();
}
