/* Diagnostic-only snapshot. No per-frame I/O, no synthetic input or graphics settings. */
#include "live-diag.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define CAP 131072
typedef struct { uint64_t ns; const char *kind; int n; double v[16]; } Row;
static Row *rows;
static size_t used;
static atomic_int active;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t done = PTHREAD_COND_INITIALIZER;
static pthread_t writer;
static int started, finish, fd = -1;
static unsigned seconds;
static uint64_t start_ns;

uint64_t diag_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}
int diag_active(void) { return atomic_load_explicit(&active, memory_order_relaxed); }
void diag_event(const char *kind, int n, const double *v) {
    if (!diag_active()) return;
    pthread_mutex_lock(&lock);
    if (diag_active() && used < CAP) {
        Row *r = &rows[used++];
        r->ns = diag_ns(); r->kind = kind; r->n = n > 16 ? 16 : n;
        memcpy(r->v, v, (size_t)r->n * sizeof(double));
        if (used == CAP) { atomic_store(&active, 0); pthread_cond_signal(&done); }
    }
    pthread_mutex_unlock(&lock);
}
static void *write_rows(void *unused) {
    (void)unused;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += seconds;
    pthread_mutex_lock(&lock);
    while (!finish && diag_active()) {
        if (pthread_cond_timedwait(&done, &lock, &deadline)) break;
    }
    atomic_store(&active, 0);
    pthread_mutex_unlock(&lock);
    FILE *f = fdopen(fd, "w");
    if (f) {
        fprintf(f, "# monotonic_ns,event,numeric_values; start_ns=%llu limit_s=%u rows=%zu cap=%d\n",
                (unsigned long long)start_ns, seconds, used, CAP);
        for (size_t i = 0; i < used; i++) {
            Row *r = &rows[i];
            fprintf(f, "%llu,%s", (unsigned long long)r->ns, r->kind);
            for (int k = 0; k < r->n; k++) fprintf(f, ",%.17g", r->v[k]);
            fputc('\n', f);
        }
        int bad = ferror(f);
        if (fclose(f)) bad = 1;
        fprintf(stderr, "live-diag: capture stopped, %zu rows%s%s\n", used,
                used == CAP ? " (capacity reached)" : "", bad ? " (WRITE ERROR)" : "");
    } else { close(fd); fprintf(stderr, "live-diag: fdopen failed\n"); }
    free(rows); rows = NULL;
    return NULL;
}
void diag_init(void) {
    const char *path = getenv("LORD_DIAG_FILE");
    if (!path || !*path) return;
    const char *duration = getenv("LORD_DIAG_SECONDS");
    long value = duration ? strtol(duration, NULL, 10) : 120;
    seconds = value >= 10 && value <= 600 ? (unsigned)value : 120;
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) { perror("live-diag: output"); return; }
    rows = calloc(CAP, sizeof *rows);
    if (!rows) { close(fd); return; }
    start_ns = diag_ns();
    atomic_store(&active, 1);
    if (pthread_create(&writer, NULL, write_rows, NULL)) {
        atomic_store(&active, 0); free(rows); rows = NULL; close(fd); return;
    }
    started = 1;
    fprintf(stderr, "live-diag: recording for at most %u s to %s\n", seconds, path);
}
void diag_finish(void) {
    if (!started) return;
    pthread_mutex_lock(&lock); finish = 1; pthread_cond_signal(&done); pthread_mutex_unlock(&lock);
    pthread_join(writer, NULL); started = 0;
}
