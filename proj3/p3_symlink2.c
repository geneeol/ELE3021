#include "param.h"
#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "user.h"

void test_hardlink()
{
  int fd;

  printf(1, "hard link test: change file contents\n");
  fd = open("test", O_RDWR);
  write(fd, "hello\n", 7);
  close(fd);
}

void test_symlink1()
{
  int fd;

  printf(1, "sym link test: change file contents\n");
  printf(1, "open symlink file test2 and change contents\n");
  fd = open("test2", O_RDWR);
  write(fd, "hello\n", 7);
  close(fd);
}

// 사전에 echo [string] > test, ln [-sh] test test2로 파일을 만들어야 한다.
int main(int argc, char *argv[])
{
  // test_hardlink();
  test_symlink1();
  exit();
}