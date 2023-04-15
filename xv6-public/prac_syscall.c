#include "types.h"
#include "defs.h"

int	safeprint(char *str)
{
	cprintf("%s\n", str);
	return (0xABCDABCD);
}

int	sys_safeprint(void)
{
	char	*str;

	if (argstr(0, &str) < 0)
		return (-1);
	return (safeprint(str));
}