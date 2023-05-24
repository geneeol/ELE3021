#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
	sbrk(4096);
	exit();
}