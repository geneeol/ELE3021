// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork(); //h sh의 pid값이 저장된다
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){ //h 자식 프로세스는 sh.c 를 실행 (실제로 쉘 역할은 한다)
      exec("sh", argv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid) //h 자식프로세스 sh가 종료될 때까지 대기한다.
      printf(1, "zombie!, orphan: %d\n", wpid);                //h sh를 회수하면 반복문을 바로 탈출한다
  }
}
