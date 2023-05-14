// Test that fork fails gracefully.
// Tiny executable so that the limit can be filling the proc table.

#include "types.h"
#include "stat.h"
#include "user.h"

#define N  30

void	distribution(int *freq)
{
	printf(1, "Process %d\n", getpid());
	for (int i = 0; i < 3; i++)
		printf(1, "L%d: %d\n", i, freq[i]);
	printf(1, "\n");
}

void
forktest(void)
{
  int n, pid;

  printf(1, "fork test\n");

  for(n=0; n<N; n++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0)
	  {
		sleep(pid * 10);
  		int freq[4] = {0, 0, 0, 0};
		for (int i = 0; i < 1000; i++)
			freq[getLevel()]++;
		distribution(freq);
	  }
  }

  if(n == N){
    printf(1, "fork claimed to work N times!\n", N);
    exit();
  }
  int freq[4] = {0, 0, 0, 0};

  wait();
  for(; n > 0; n--){

	freq[getLevel()]++;
	distribution(freq);
    if(wait() < 0){
      printf(1, "wait stopped early\n");
      exit();
    }
  }

  if(wait() != -1){
    printf(1, "wait got too many\n");
    exit();
  }

  printf(1, "fork test OK\n");
}

int
main(void)
{
  bp_tracer("test_mlfq3 starts");
  forktest();
  exit();
}