#define _GNU_SOURCE   /* pthread_setname_np */
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <pthread.h>
#endif
#include <SDL2/SDL.h>

#include "vita_platform.h"
#include "vita_perf.h"
#include "vita_audio.h"
#include "vita_input.h"
#include "platform/vita_extras.h"
#include "gba/io_reg.h"

#define VITA_LOG_PATH VITA_DATA_DIR "debug.log"

#ifdef VITA_LOGGING
static FILE *sLogFile = NULL;
static char sLogBuffer[32768];
#endif

void VitaPlatform_Log(const char *const fmt, ...)
{
#ifdef VITA_LOGGING
    if (sLogFile == NULL)
    {
        sLogFile = fopen(VITA_LOG_PATH, "a");
        if (sLogFile == NULL)
        {
            return;
        }
        setvbuf(sLogFile, sLogBuffer, _IOFBF, sizeof(sLogBuffer));
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(sLogFile, fmt, args);
    va_end(args);
#else
    (void)fmt;
#endif
}

void VitaPlatform_Init(void)
{
    // Data directory is relative to the working directory (VITA_DATA_DIR = "userdata/").
    (void)mkdir("userdata", 0777);
    (void)mkdir("userdata/saves", 0777);
#ifdef VITA_LOGGING
    remove(VITA_LOG_PATH);
#endif

    // Initialize native input (SDL game controller and touch)
    if (!VitaInput_Init())
    {
        VITA_LOG("[INIT] VitaInput_Init reported failure!\n");
    }

    // Initialize audio subsystem
    if (!VitaAudio_Init())
    {
        VITA_LOG("[INIT] VitaAudio_Init reported failure!\n");
    }

    struct VitaLocalTime now;
    if (VitaPlatform_GetLocalTime(&now) == 0)
    {
        VITA_LOG("[RTC] Local time: %04d-%02d-%02d %02d:%02d:%02d (Day of week: %d)\n",
                 now.year, now.month, now.day, now.hour, now.minute, now.second, now.dayOfWeek);
    }

    VitaExtras_Init();
    VITA_LOG("[INIT] Deck platform initialized successfully. Data dir: %s\n", VITA_DATA_DIR);
}

void VitaPlatform_Exit(void)
{
#ifdef VITA_LOGGING
    if (sLogFile != NULL)
    {
        fflush(sLogFile);
        fclose(sLogFile);
        sLogFile = NULL;
    }
#endif
    VitaPerf_Shutdown();
    VitaAudio_Terminate();
    SDL_Quit();
    exit(0);
}

void VitaPlatform_BuildPath(char *const dest, const size_t size, const char *const relative)
{
    if (dest == NULL || relative == NULL || size == 0U)
    {
        return;
    }
    snprintf(dest, size, "%s%s", VITA_DATA_DIR, relative);
}

uint8_t *VitaPlatform_LoadAsset(const char *const relativePath, size_t *const outSize)
{
    if (outSize != NULL)
    {
        *outSize = 0U;
    }
    if (relativePath == NULL || relativePath[0] == '\0')
    {
        return NULL;
    }

    char fullPath[256];
    snprintf(fullPath, sizeof(fullPath), "app0/%s", relativePath);
    FILE *file = fopen(fullPath, "rb");
    if (file == NULL)
    {
        snprintf(fullPath, sizeof(fullPath), "%s%s", VITA_DATA_DIR, relativePath);
        file = fopen(fullPath, "rb");
    }
    if (file == NULL)
    {
        snprintf(fullPath, sizeof(fullPath), "app0/assets/%s", relativePath);
        file = fopen(fullPath, "rb");
    }
    if (file == NULL)
    {
        return NULL;
    }

    if (fseek(file, 0L, SEEK_END) != 0)
    {
        fclose(file);
        return NULL;
    }
    const long rawSize = ftell(file);
    if (rawSize <= 0L || fseek(file, 0L, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }

    const size_t size = (size_t)rawSize;
    uint8_t *const buf = (uint8_t *)malloc(size);
    if (buf == NULL)
    {
        fclose(file);
        return NULL;
    }

    const size_t readBytes = fread(buf, 1U, size, file);
    fclose(file);
    if (readBytes != size)
    {
        free(buf);
        return NULL;
    }

    if (outSize != NULL)
    {
        *outSize = size;
    }
    return buf;
}

uint32_t VitaPlatform_ReadButtons(void)
{
    VitaInput_Poll(NULL);
    return VitaInput_GetButtonsHeld();
}

void VitaPlatform_PinCurrentThread(const char *const name, const int32_t core)
{
    // No core pinning on the Deck; the OS scheduler decides. Only name the thread.
#ifdef __linux__
    if (name != NULL)
    {
        (void)pthread_setname_np(pthread_self(), name);
    }
#endif
    VITA_LOG("[THREAD] %s: core %d requested, pinning not supported on Deck\n", name, core);
    (void)name;
    (void)core;
}

void VitaPlatform_DelayMicroseconds(const uint32_t microseconds)
{
    if (microseconds == 0U)
    {
        return;
    }
    struct timespec ts = { microseconds / 1000000U, (long)(microseconds % 1000000U) * 1000L };
    nanosleep(&ts, NULL);
}

int32_t VitaPlatform_GetCpuMHz(void)
{
    // The Deck's clock is not controlled by the game; report a fixed nominal value.
    return 3500;
}

int32_t VitaPlatform_GetLocalTime(struct VitaLocalTime *const out)
{
    if (out == NULL)
    {
        return -1;
    }

    time_t t = time(NULL);
    struct tm tmv;
    if (localtime_r(&t, &tmv) == NULL)
    {
        return -1;
    }

    out->year      = tmv.tm_year + 1900;
    out->month     = tmv.tm_mon + 1;
    out->day       = tmv.tm_mday;
    out->dayOfWeek = tmv.tm_wday;
    out->hour      = tmv.tm_hour;
    out->minute    = tmv.tm_min;
    out->second    = tmv.tm_sec;
    return 0;
}

bool VitaPlatform_ReadTouch(int32_t *const outX, int32_t *const outY)
{
    return VitaInput_ReadTouchScreen(outX, outY);
}

bool VitaPlatform_ReadTouchGBA(int32_t *const outGbaX, int32_t *const outGbaY)
{
    return VitaInput_ReadTouchGBA(outGbaX, outGbaY);
}

uint64_t VitaPlatform_GetMilliseconds(void)
{
    static uint64_t sStartMs;
    static bool sStarted;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t now = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    if (!sStarted)
    {
        sStartMs = now;
        sStarted = true;
    }
    return now - sStartMs;
}

// Epoch finding (Step 5): the only callers are vita_daynight.c (lines ~178, 300, 314, 320). They use the
// value solely as (a) a "> 0" validity check, (b) a difference between two samples, and (c) a field in the
// save-game snapshot (wallSeconds), later subtracted from a fresh sample on load. Nothing compares it with
// time() or any Unix-epoch value. Sony's tick counts from year 1, so the offset below keeps snapshots
// written on the Vita and on the Deck on the same epoch (restoring a Vita save yields a sane delta).
int64_t VitaPlatform_GetWallSeconds(void)
{
    return (int64_t)time(NULL) + 62135596800LL;
}
