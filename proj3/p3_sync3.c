#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

#define PATH "synctest"


int
main(void)
{
	int fd;

	fd = open (PATH, O_CREATE | O_RDWR);
	write(fd, "hello", 5);
	close(fd);
	// sync();
	exit();
}
