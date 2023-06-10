#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

int
main(void)
{
  int fd;

  fd = open("syncfile", O_CREATE | O_RDWR);
  if (fd < 0) {
    printf(1, "syncfile: cannot open\n");
    exit();
  }
  write(fd, "hi\n", 4);
  sync();
  exit();
}