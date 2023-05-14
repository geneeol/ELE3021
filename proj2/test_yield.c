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
			safeprint("child process\n"); // 출력을 보장
			yield();
		}
	}
	else
	{
		for (int i = 0; i < 10; i++)
		{
			safeprint("parent process\n");
			yield();
		}
		wait();
	}
	exit();

}