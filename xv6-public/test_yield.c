#include "types.h"
#include "stat.h"
#include "user.h"

int	main(void)
{
	int	pid;

	pid = fork();
	if (pid < 0)
		exit();
	if (pid == 0)
	{
		for (int i = 0; i < 10; i++)
		{
			yield();
			safeprint("child process\n"); // 출력을 보장
		}
	}
	else
	{
		for (int i = 0; i < 10; i++)
		{
			yield();
			safeprint("parent process\n");
		}
		wait();
	}
	exit();

}