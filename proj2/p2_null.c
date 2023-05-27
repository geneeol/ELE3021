#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
	int *ptr;

	ptr = malloc(4096);
	ptr = 0;
	ptr[0] = 1;
	printf(1, "ptr = %d\n", *ptr);
	exit();
}