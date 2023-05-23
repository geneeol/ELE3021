#include "types.h"
#include "stat.h"
#include "user.h"

int g_cnt;
// 전역변수 하나 추가하면 exec에서 맨 처음 sz값이 4바이트씩 늘어남.
int g_cnt2;

void *routine(void *arg)
{
  int x;

  x = 10;
  printf(1, "thread's variable x addr: %x\n", (uint)&x);
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
  int *ptr;

  // 주소를 직접 할당해서 접근.
  ptr = (int *)0x4FE8;
  i = 10;
  if (thread_create(&tid, routine, &i))
    printf(1, "thread_create failed\n");
  // thread_join(tid, 0);
  printf(1, "I'm main thread\n");
  sleep(20);
  printf(1, "\nglobal variable increased by 1 by thread\ng_cnt: %d\n", g_cnt);
  printf(1, "main thread try to access thread's local variale x by its addr\nx: %d\n", *ptr);
  thread_join(tid, 0);
  exit();
}