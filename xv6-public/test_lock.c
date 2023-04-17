#include "types.h"
#include "stat.h"
#include "user.h"

#define ID 2019035587

void	distribution(int *freq)
{
	printf(1, "Process %d\n", getpid());
	for (int i = 0; i < 3; i++)
		printf(1, "L%d: %d\n", i, freq[i]);

}

void lock_test1(void)
{
	int	freq[4] = {0, 0, 0, 0};
	int	pid;
	int	x;

	pid = fork();
	if (pid == 0)
	{
		printf(1, "child: pid: %d, try lock\n", getpid());
		schedulerLock(ID);
		printf(1, "child: pid: %d, get lock\n", getpid());
		for (int i = 0; i < 10000; i++) // 루프 1만번 정도에 부스팅 1회 발생
		{
			x = getLevel();
			if (x < 0 || x > 3)
			{
				printf(1, "Wrong level\n");
				continue ;
			}
			freq[x]++;
			if (i % 100 == 0)
				printf(1, "elapsed loop: %d\n", i);
		}
		printf(1, "loop finished");
		distribution(freq);
		schedulerUnlock(ID);
	}
	else if (pid > 0)
	{
		sleep(20); // sleeep도 cpu를 사용하면서 대기함, 따라서 l0큐에서 해당 틱을 전부 소모
		printf(1, "parent: pid: %d\n", getpid());
		for (int i = 0; i < 10000; i++)
		{
			x = getLevel();
			if (x < 0 || x > 3)
			{
				printf(1, "Wrong level\n");
				continue ;
			}
			freq[x]++;
			if (i % 100 == 0)
				printf(1, "parent: pid: %d, i: %d\n", getpid(), i);
		}
		distribution(freq);
		wait();
	}
	else
		printf(1, "failed to fork!!\n");
}

int	main(void)
{

	lock_test1();
	exit();
}