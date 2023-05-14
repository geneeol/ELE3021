#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

#define NUM_LOOP 100000
#define NUM_YIELD 20000
#define NUM_SLEEP 1000

#define NUM_THREAD 6
#define MAX_LEVEL 3

int parent;

int fork_children()
{
  int i, p;
  for (i = 0; i < NUM_THREAD; i++)
    if ((p = fork()) == 0)
    {
      sleep(10);
      return getpid();
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

  printf(1, "MLFQ test start\n");

  printf(1, "[Test 1] default\n");
  pid = fork_children();

  if (pid != parent)
  {
	if (pid % 2 == 0)
	{
		for (i = 0; i < NUM_LOOP; i++)
			count[getLevel()]++;
	}
  else
  {
    for (i = 0; i < NUM_LOOP / 100; i++)
    {
      sleep(5);
			count[getLevel()]++;
    }
  }
  schedulerLock(PASSWORD);
  printf(1, "Process %d\n", pid);
  for (i = 0; i < MAX_LEVEL; i++)
    printf(1, "L%d: %d\n", i, count[i]);
  }
  schedulerUnlock(PASSWORD);
  exit_children();
  printf(1, "[Test 1] finished\n");
  printf(1, "done\n");
  exit();
}
