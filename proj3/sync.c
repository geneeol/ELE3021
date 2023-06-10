#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

int
main(void)
{
  int ret;

  ret = sync();
  printf(1, "sync returned: %d\n", ret);
  exit();
}