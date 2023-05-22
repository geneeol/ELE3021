#include "types.h"
#include "stat.h"
#include "user.h"

int g_cnt;

void *routine(void *arg)
{
  printf(1, "I'm new thread, arg: %d\n", *(int *)arg);
  g_cnt++;
  // sleep(100);
  thread_exit(0);
  return (0);
}

int
main(int argc, char *argv[])
{
  thread_t tid;
  int i;

  i = 10;
  if (thread_create(&tid, routine, &i))
    printf(1, "thread_create failed\n");
  // thread_join(tid, 0);
  printf(1, "I'm main thread\n");
  sleep(10);
  printf(1, "g_cnt: %d\n", g_cnt);
  exit();
}