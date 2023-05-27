#include "types.h"
#include "stat.h"
#include "user.h"

void
do_test0(void)
{
	int pid;
  char *argv2[2] = {"nothing", 0};

  pid = fork();
  if (pid < 0)
  {
    printf(1, "fork failed\n");
    exit();
  }
  else if (pid == 0)
  {
    if (exec("thread_exec", argv2))
      printf(1, "exec failed\n");
  }
  else
    wait();
}

void
do_test1(void)
{
	int pid;
  char *argv2[2] = {"nothing", 0};

  pid = fork();
  if (pid < 0)
  {
    printf(1, "fork failed\n");
    exit();
  }
  else if (pid == 0)
  {
    if (exec("thread_exit", argv2))
      printf(1, "exec failed\n");
  }
  else
    wait();
}

void
do_test2(void)
{
	int pid;
  char *argv2[2] = {"nothing", 0};

  pid = fork();
  if (pid < 0)
  {
    printf(1, "fork failed\n");
    exit();
  }
  else if (pid == 0)
  {
    if (exec("thread_kill", argv2))
      printf(1, "exec failed\n");
  }
  else
    wait();
}

void
do_test3(void)
{
	int pid;
  char *argv2[2] = {"nothing", 0};

  pid = fork();
  if (pid < 0)
  {
    printf(1, "fork failed\n");
    exit();
  }
  else if (pid == 0)
  {
    if (exec("thread_test", argv2))
      printf(1, "exec failed\n");
  }
  else
    wait();
}

// exec에 원하는 실행파일을 넣어 반복적으로 실행하는 테스트.
int
main(int argc, char *argv[])
{
  void (*test[])(void) = {do_test0, do_test1, do_test2, do_test3};
  int start;
  int end;

  start = 0;
  end = sizeof(test) / sizeof(void (*)(void)) - 1;

  if (argc > 1)
    start = atoi(argv[1]);
  if (argc > 2)
    end = atoi(argv[2]);
  while (1)
  {
    for (int i = start; i <= end; i++)
    {
      printf(1, "Test %d start\n", i);
      test[i]();
      printf(1, "Test %d end\n\n\n", i);
    }
  }
  exit();
}