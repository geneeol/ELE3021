#include "types.h"
#include "stat.h"
#include "user.h"

int	main(int argc, char *argv[])
{
	int		ret_val;

	// The number of arguments should be at least two.
	if (argc <= 1)
		exit();
	// Passing argv[1] to safeprint (We've made it before)
	// then safeprint will print argv[1].
	ret_val = safeprint(argv[1]);
	printf(1, "Return value: 0x%x\n", ret_val);
	exit();
}