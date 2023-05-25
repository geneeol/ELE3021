#include "types.h"
#include "stat.h"
#include "user.h"

thread_t tid[10];

void *
routine(void *arg)
{
  printf(1, "thread is going to sleep %d\n", (int)arg);
  sleep(1000);
  thread_exit(0);
  return (0);
}

// main에서 exec2를 호출하는 코드
int
main(int argc, char **argv)
{
  char *argv2[] = {"hello_thread", 0};
	for (int i = 0 ; i < 10; i++)
		thread_create(&tid[0], routine, (void*)i);
  sleep(100);
  exec2("hello_thread", argv2, 5);
  exit();
}