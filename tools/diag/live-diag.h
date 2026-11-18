#ifndef LORD_LIVE_DIAG_H
#define LORD_LIVE_DIAG_H
#include <stdint.h>
void diag_init(void);
void diag_finish(void);
int diag_active(void);
uint64_t diag_ns(void);
void diag_event(const char *kind, int count, const double *values);
#define DIAG(kind, ...) do { if (diag_active()) { \
    const double diag_values_[] = { __VA_ARGS__ }; \
    diag_event(kind, sizeof(diag_values_) / sizeof(double), diag_values_); \
} } while (0)
#endif
