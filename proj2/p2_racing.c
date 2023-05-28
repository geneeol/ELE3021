#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREADS (int)20
#define NUM_ITERATIONS (int)1000000

int shared_var = 0;

void *increment_shared_var(void *arg) {
    int val;

    val = (int)arg;
    printf(1, "Thread %d starting\n", val);

    for(int i=0; i<NUM_ITERATIONS; i++) {
        if (i == 0)
            printf(1, "Thread %d: i addr: 0x%x is going to sleep\n", val, (int)&i);
        if (i % 10000 == 0)
        {
            printf(1, "Thread %d: i: %d, shared_var: %d\n", val, i, shared_var);
            sleep(1);
        }
        shared_var++;
    }
    printf(1, "Thread %d end\n", val);
    thread_exit(0);
    return (0);
}

int main() {
    thread_t threads[NUM_THREADS];

    for(int i=0; i<NUM_THREADS; i++) {
        if(thread_create(&threads[i], increment_shared_var, (void *)i) != 0) {
            printf(2, "Error creating thread\n");
        }
    }

    for(int i=0; i<NUM_THREADS; i++) {
        if(thread_join(threads[i], 0) != 0) {
            printf(2, "Error joining thread\n");
        }
    }

    printf(1, "Expected value: %d\n", NUM_THREADS * NUM_ITERATIONS);
    printf(1, "Actual value: %d\n", shared_var);
    exit();

    return 0;
}
