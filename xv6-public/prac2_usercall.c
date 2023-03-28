#include "types.h"
#include "stat.h"
#include "user.h"

// SETGATE(idt[T_PRAC2], 1, SEG_KCODE<<3, vectors[T_PRAC2], DPL_USER);
// By adding this line, privilege level can be changed user to kernel
// (This is the way system call works.)
int
main(int argc, char *argv[])
{
	__asm__("int $128");
	exit();
}