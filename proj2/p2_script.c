#include "types.h"
#include "stat.h"
#include "user.h"

// exec에 원하는 실행파일을 넣어 반복적으로 실행하는 테스트.
int
main(int argc, char *argv[])
{
	int pid;
  char *argv2[2] = {"nothing", 0};
  // int cnt;

  while (1)
  {
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
  exit();
}