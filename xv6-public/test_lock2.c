#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

void lock_test1()
{
	for (int i = 0; i < 5; i++)
	{
		schedulerLock(PASSWORD);
		for (int i = 0; i < 10000000; i++) // 루프가 충분히 커야 부스팅에 의해 언락됨
		{
			if (i % 100000 == 0)
				printf(1, "waiting for boosting to occur unlocking: %d\n", i);
		}
		schedulerLock(PASSWORD);
		schedulerUnlock(PASSWORD);
		schedulerUnlock(PASSWORD);
	}
	printf(1, "lock_test1 finished\n");
}

int	main(void)
{
	bp_tracer("test_lock2 starts\n");
	lock_test1();
	exit();
}