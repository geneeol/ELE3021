#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

#define BUFSIZE 7

int main(int argc, char const *argv[])
{
  int fd;
  char data[BUFSIZE];
  int cnt;
  const int sz = sizeof(data);

  fd = open("syncfile", O_CREATE | O_RDWR);
  if (fd < 0)
  {
    printf(1, "syncfile: cannot open\n");
    exit();
  }

  for (int i = 0; i < sz; i++)
  {
    if (i % 2 == 0)
      data[i] = 'a';
    else
      data[i] = 'b';
  }
  cnt = 0;
  while (1)
  {
    if (cnt % 100 == 0)
    {
      printf(1, "%d bytes written\n", cnt * BUFSIZE);
    }
    if (write(fd, data, sz) != sz)
    {
      printf(1, "write returned: %d: failed\n", sz);
      exit();
    }
    cnt++;
  }
  exit();
}
