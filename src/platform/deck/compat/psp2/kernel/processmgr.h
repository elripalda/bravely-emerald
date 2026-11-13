#ifndef DECK_COMPAT_PSP2_KERNEL_PROCESSMGR_H
#define DECK_COMPAT_PSP2_KERNEL_PROCESSMGR_H

#include <stdint.h>
#include <time.h>

// replaces sceKernelGetProcessTimeWide (vita_perf.c:38): monotonic time in microseconds.
static inline uint64_t sceKernelGetProcessTimeWide(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

#endif
