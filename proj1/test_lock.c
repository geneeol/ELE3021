#include "types.h"
#include "stat.h"
#include "user.h"

#define ID 2019035587
#define N_LOOP 100000

//h 중요! 터미널 출력이 자식, 부모 한쪽에 몰려있어도 실제로는 그 시간 사이에 부스팅 및 스위칭 발생함. gdb로 확인됨
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
		printf(1, "c pid: %d, before try lock\n", getpid());
		// schedulerLock(ID);
		__asm__("int $129");
		printf(1, "c pid: %d, after try lock\n", getpid());
		for (int i = 0; i < N_LOOP; i++) // 루프 1만번 정도에 부스팅 1회 발생
		{
			freq[getLevel()]++;
			if (i % 5000 == 0)
				printf(1, "c elapsed: %d\n", i);
		}
		printf(1, "loop finished");
		distribution(freq);
		printf(1, "c pid: %d, before try unlock\n", getpid());
		__asm__("int $130");
		printf(1, "c pid: %d, after try unlock\n", getpid());
		// schedulerUnlock(ID);
	}
	else if (pid > 0)
	{
		sleep(20); // sleeep도 cpu를 사용하면서 대기함, 따라서 l0큐에서 해당 틱을 전부 소모
		printf(1, "p: pid: %d\n", getpid());
		for (int i = 0; i < N_LOOP; i++)
		{
			freq[getLevel()]++;
			if (i % 5000 == 0)
				printf(1, "p elapsed: %d\n", i);
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