#include "spinlock.h"

struct stat;
struct rtcdate;

typedef int thread_t;


// 시스템콜추가: 시스템콜 헤더 추가, makefile에 추가
// system calls
int	fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int	safeprint(char *);

// project1 추가한 시스템콜
int yield(void);
int	getLevel(void);
void setPriority(int pid, int priority);
void schedulerLock(int password);
void schedulerUnlock(int password);
void bp_tracer(const char *msg);
void mutex_init(struct spinlock *lk, char *name);
void mutex_lock(struct spinlock *lk);			
void mutex_unlock(struct spinlock *lk);

// project2 추가한 시스템콜
int exec2(char *path, char **argv, int stacksize);
int setmemorylimit(int pid, int limit);
int	plist(void);
int thread_create(thread_t *thread, 
					void *(*start_routine)(void *),
					void *arg);
void thread_exit(void *retval);
int thread_join(thread_t thread, void **retval);
int get_pid(void);
int get_tid(void);



// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
