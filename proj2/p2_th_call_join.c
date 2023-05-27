#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 10

thread_t thread[NUM_THREAD];

// 쓰레드 5가 메인 쓰레드 이전에 thread[0]을 회수하는 상황.
void *routine(void *arg)
{
	int val = (int)arg;
	void *retval; 

	printf(1, "Thread %d start\n", val);

	if (val == 5)
	{
		if (thread_join(thread[0], &retval) == -1)
		{
			printf(1, "Thread %d join failed\n", val);
			thread_exit(arg);
		}
		printf(1, "Thread %d join sucess, target: %d retval: %d\n", val, thread[0], (int)retval);
	}
	thread_exit(arg);
	return 0;
}
int
main(void)
{
	for (int i = 0; i < NUM_THREAD; i++)
		thread_create(&thread[i], routine, (void *)i);
	sleep(200);
	for (int i = 0; i < NUM_THREAD; i++)
	{
		if (thread_join(thread[i], 0) == -1)
			printf(1, "main Thread %d join failed, target: %d\n", i, thread[i]);
	}
	exit();
}