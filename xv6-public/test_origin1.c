#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

// #define NUM_LOOP 100000
#define NUM_LOOP 50000
#define NUM_YIELD 20000
#define NUM_SLEEP 1000

#define NUM_THREAD 40
#define MAX_LEVEL 5

int parent;
int g_cnt[MAX_LEVEL];

int fork_children()
{
  int i, p;
  int x;
  for (i = 0; i < NUM_THREAD; i++)
  {
    x = getLevel(); // 여기 출력 넣을 때 trap 14 발생.. 오ㅑ지?
    g_cnt[x]++;
    if ((p = fork()) == 0)
    {
      // sleep(10);
      return getpid();
    }
    // else // fork가 -1 리턴했을 때 getLevel하면 에러 발생했갰다.. 맞나?
    // {
    x = getLevel(); // 여기 출력 넣을 때 trap 14 발생.. 오ㅑ지?
    if (x >= 0)
    {
      g_cnt[x]++;
      // printf(1, "hi g_cnt[%d] = %d\n", x, g_cnt[x]);
    }
    else  
      printf(1, "Wrong level: %d\n", x);
    // }
  }
  return parent;
}


int fork_children2()
{
  int i, p;
  for (i = 0; i < NUM_THREAD; i++)
  {
    if ((p = fork()) == 0)
    {
      sleep(300);
      return getpid();
    }
    else
      setPriority(p, i);
  }
  return parent;
}

int max_level;

int fork_children3()
{
  int i, p;
  for (i = 0; i < NUM_THREAD; i++)
  {
    if ((p = fork()) == 0)
    {
      sleep(10);
      max_level = i;
      return getpid();
    }
  }
  return parent;
}
void exit_children()
{
  if (getpid() != parent)
    exit();
  while (wait() != -1);
}

int main(int argc, char *argv[])
{
  int i, pid;
  int count[MAX_LEVEL] = {0};
//  int child;

  parent = getpid();

  bp_tracer("test_origin1 starts");
  printf(1, "MLFQ test start\n");

  printf(1, "[Test 1] default\n");
  pid = fork_children();

  if (pid != parent)
  {
    for (i = 0; i < NUM_LOOP; i++)
    {
      int x = getLevel();
      if (x < 0 || x > 4)
      {
        printf(1, "Wrong level: %d\n", x);
        exit();
      }
      count[x]++;
    }
    schedulerLock(PASSWORD);
    printf(1, "Process %d\n", pid);
    for (i = 0; i < MAX_LEVEL; i++)
      printf(1, "L%d: %d\n", i, count[i]);
    schedulerUnlock(PASSWORD);
  }
  exit_children();
  printf(1, "Process partent %d\n", pid);
    for (i = 0; i < MAX_LEVEL; i++)
      printf(1, "p L%d: %d\n", i, g_cnt[i]);
  bp_tracer("test_origin1 ends");
  printf(1, "[Test 1] finished\n");
  printf(1, "done\n");
  exit();
}