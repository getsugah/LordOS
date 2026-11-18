#include "live-diag.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

static void *produce(void *arg) {
    long id = (long)arg;
    for (int i = 0; i < 40000; i++) DIAG("test", id, i);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    unsetenv("LORD_DIAG_FILE");
    diag_init(); assert(!diag_active());
    setenv("LORD_DIAG_FILE", argv[1], 1);
    setenv("LORD_DIAG_SECONDS", "10", 1);
    diag_init(); assert(diag_active());
    pthread_t workers[4];
    for (long i = 0; i < 4; i++) assert(!pthread_create(&workers[i], NULL, produce, (void *)i));
    for (int i = 0; i < 4; i++) pthread_join(workers[i], NULL);
    diag_finish(); assert(!diag_active());
    DIAG("after_finish", 1);
    /* Existing captures must not be overwritten. */
    diag_init(); assert(!diag_active());
    puts("Recorder concurrency/capacity/disabled/exclusive-output checks passed");
    return 0;
}
