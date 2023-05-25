#include "types.h"
#include "stat.h"
#include "user.h"

thread_t thread[10];

void *
start_routine(void *arg)
{
  int val;

  val = (int)arg;
  if (val == 1)
  {
    sleep(1000);
    exit();
  }
  if (val == 2)
  {
    // TODO:: join과 exit이 경쟁하는 상황
    // 만약 exit이 먼저 호출되면 2가지 경우
    // 1. 메인 쓰레드가 exit을 호출했을 경우 서브 쓰레드가 모두 자연히 정리됨.
    // (kill에 의해 join 코드로 진행되지 않음.)
    // 2. 쓰레드가 exit을 호출하는 경우 메인 쓰레드 kill 플래그를 세움.
    // 이제 다시 메인 쓰레드의 exit과 join이 경쟁하게 됨.
    // 2-1 만약 메인 쓰레드가 먼저 exit하면 서브 쓰레드는 join은 실행되지 않음.
    // 2-2 만약 join이 먼저 실행되면 join이 자원을 회수하거나 sleep
    // 이후 exit이 실행돼서 결국 결과는 같음.
    
    // 만약 join이 먼저 호출되는 경우.
    // join으로 자원을 회수하거나 sleep. 이후 exit이 호출되면 위 2-2 상황과 같음.
    if (thread_join(thread[0], 0))
      printf(1, "join failed\n");

    exit();
  }
  return (0);
}

int
main(void)
{
	thread_create(&thread[0], start_routine, (void *)1);
	thread_create(&thread[1], start_routine, (void *)2);
  sleep(1000);
  exit();
}