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

void lock_test1(void) //h 100 번에 1번 프린트, 토탈 3번 프린트하는게 대충 3틱
{
	int	freq[4] = {0, 0, 0, 0};
	int	pid;

	pid = fork();
	if (pid == 0)
	{
		printf(1, "child: pid: %d, try lock\n", getpid());
		schedulerLock(ID);
		printf(1, "child: pid: %d, get lock\n", getpid());
		for (int i = 0; i < 10000; i++) // 루프 1만번 정도에 부스팅 1회 발생
		{
			freq[getLevel()]++;
			if (i % 100 == 0)
				printf(1, "child elapsed: %d\n", i);
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
			freq[getLevel()]++;
			if (i % 100 == 0)
				printf(1, "parent elapsed: i: %d\n", i);
		}
		distribution(freq);
		wait();
	}
	else
		printf(1, "failed to fork!!\n");
}

int	main(void)
{
	bp_tracer("test_lock starts");
	lock_test1();
	exit();
}