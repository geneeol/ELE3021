#include "types.h"
#include "stat.h"
#include "user.h"


//xv6는 free한 이후 해당 포인터에 다시 접근해도 에러가 발생하지 않는다 ㅇㅁㅇ
int
main(int argc, char **argv)
{
	int *ptr;

	ptr = (int *)malloc(4096);
	ptr[0] = 1;
	printf (1, "ptr[0]: %d\n", ptr[0]);
	free(ptr);
	ptr[100] = 2;
	printf (1, "ptr[0]: %d, ptr[100]: %d\n", ptr[0], ptr[100]);
	exit();
}