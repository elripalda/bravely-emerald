#ifndef DECK_COMPAT_PSP2_KERNEL_PROCESSMGR_H
#define DECK_COMPAT_PSP2_KERNEL_PROCESSMGR_H

#include <stdint.h>
#include <time.h>
#ifdef _WIN32
#include <SDL2/SDL_timer.h>   // QueryPerformanceCounter through SDL
#endif

// replaces sceKernelGetProcessTimeWide (vita_perf.c:38): monotonic time in microseconds.
static inline uint64_t sceKernelGetProcessTimeWide(void)
{
#ifdef _WIN32
    return SDL_GetPerformanceCounter() * 1000000ULL / SDL_GetPerformanceFrequency();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
#endif
}

#endif
