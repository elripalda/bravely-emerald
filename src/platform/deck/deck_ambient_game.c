#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "main.h"
#include "overworld.h"
#include "fieldmap.h"
#include "field_player_avatar.h"
#include "metatile_behavior.h"
#include "constants/map_types.h"
#include "platform/vita_daynight.h"
#include "platform/vita_extras.h"
#include "platform/vita_ui.h"
#include "platform/ui.h"
#include "menu.h"
#include "window.h"
#include "vita_platform.h"
#include "../voxel/voxel_world.h"
#include "deck_ambient.h"
#include "deck_ambient_game.h"

// Maps the game state to the ambient layer targets (spec 3.h context table) at about 6 Hz and renders the bed.

#define UPDATE_EVERY 10      // game frames between context updates
#define WATER_EVERY 20       // game frames between water scans
#define STATS_EVERY 300      // game frames between log lines (5 s)
#define WATER_RADIUS 4

static bool sReady;
static unsigned sFrame;
static float sWater;         // 0..1 from the metatile scan
static float sPrevBolt;
static uint32_t sLastKey = 0xffffffffu;
static double sSumSq, sPeak;
static long sSamples;
static uint32_t sQMin = 0xffffffffu, sQMax;

#define MAX_LOG_LINES 2000   // per process, so a long debug session cannot grow debug.log without bound

static int sLogLines;
static int sLogOn = -1;      // -1 = env not read yet

static void Log(const char *fmt, ...)
{
    // Same switch as the GL diagnostics: DECK_GL_DEBUG set and not "0". Off = no file access at all.
    if (sLogOn < 0)
    {
        const char *dbg = getenv("DECK_GL_DEBUG");
        sLogOn = (dbg != NULL && strcmp(dbg, "0") != 0) ? 1 : 0;
    }
    if (!sLogOn || sLogLines >= MAX_LOG_LINES)
        return;
    sLogLines++;
    // VITA_LOG is fully buffered (32 KB, no flush), so short runs never show it; this appends and flushes.
    FILE *lg = fopen(VITA_DATA_DIR "debug.log", "a");
    if (lg != NULL)
    {
        va_list args;
        va_start(args, fmt);
        vfprintf(lg, fmt, args);
        va_end(args);
        fclose(lg);
    }
}

static float Ramp(float v, float lo, float hi)
{
    float t = (v - lo) / (hi - lo);
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

static float ScanWater(void)
{
    s16 px, py;
    int count = 0;

    PlayerGetDestCoords(&px, &py);
    for (int dy = -WATER_RADIUS; dy <= WATER_RADIUS; dy++)
        for (int dx = -WATER_RADIUS; dx <= WATER_RADIUS; dx++)
            if (MetatileBehavior_IsSurfableWaterOrUnderwater((u8)MapGridGetMetatileBehaviorAt(px + dx, py + dy)))
                count++;
    return Ramp((float)count, 2.0f, 14.0f);
}

static void UpdateTargets(void)
{
    DeckAmbientTargets t;
    // Menus run while gMain.callback2 is still CB2_Overworld: the Vita/Deck start menu and its screens (VitaUI), the new UI,
    // and the vanilla start menu window (the vanilla one also freezes the avatar; dialogue and scripts do not count).
    const bool menu = VitaUI_IsOpen() || Ui_IsOpen() || GetStartMenuWindowId() != WINDOW_NONE;
    const bool overworld = VitaGame_IsOverworld() && !menu;
    const bool outdoor = overworld && VitaGame_IsOutdoor();
    const bool underground = overworld && gMapHeader.mapType == MAP_TYPE_UNDERGROUND;

    memset(&t, 0, sizeof(t));
    if (outdoor)
    {
        const float daylight = VitaDayNight_Get()->daylight;
        const VoxelWeatherClass weather = VoxelWorld_Weather();
        const float amount = VoxelWorld_WeatherAmount();
        const float rain = (weather == VOXEL_WEATHER_RAIN) ? amount : 0.0f;
        const float sand = (weather == VOXEL_WEATHER_SAND) ? amount : 0.0f;
        const float bolt = VoxelWorld_Lightning();

        t.master = 1.0f;
        t.wind = (0.2f + 0.15f * daylight) + 0.35f * rain + 0.8f * sand;
        t.birds = Ramp(daylight, 0.25f, 0.75f) * (1.0f - rain) * (1.0f - sand);
        t.crickets = Ramp(0.6f - daylight, 0.0f, 0.5f) * (1.0f - 0.8f * rain) * (1.0f - sand);
        t.rain = rain;
        if (gMapHeader.mapType == MAP_TYPE_OCEAN_ROUTE)
            sWater = 1.0f;
        else if (sFrame % WATER_EVERY == 0)
            sWater = ScanWater();
        t.water = sWater;
        if (bolt > 0.5f && sPrevBolt <= 0.5f)
            DeckAmbient_Thunder();
        sPrevBolt = bolt;
    }
    else if (underground)
    {
        t.master = 1.0f;
        t.cave = 1.0f;
        sPrevBolt = 0.0f;
    }
    else
    {
        sPrevBolt = 0.0f;   // indoors, battles, menus, transitions: everything stays 0 and the master fades out
    }
    DeckAmbient_SetTargets(&t);

    {
        const uint32_t key = ((uint32_t)(t.wind * 4.0f) << 0) | ((uint32_t)(t.birds * 4.0f) << 4) | ((uint32_t)(t.crickets * 4.0f) << 8)
                           | ((uint32_t)(t.water * 4.0f) << 12) | ((uint32_t)(t.rain * 4.0f) << 16) | ((uint32_t)(t.cave * 4.0f) << 20)
                           | ((uint32_t)(t.master * 2.0f) << 24);
        if (key != sLastKey)
        {
            sLastKey = key;
            Log("ambient: mapType %d overworld %d menu %d daylight %.2f -> wind %.2f birds %.2f crickets %.2f water %.2f rain %.2f cave %.2f master %.0f\n",
                     (int)gMapHeader.mapType, (int)overworld, (int)menu, (double)VitaDayNight_Get()->daylight, (double)t.wind, (double)t.birds,
                     (double)t.crickets, (double)t.water, (double)t.rain, (double)t.cave, (double)t.master);
        }
    }
}

void DeckAmbient_Frame(float *stereo, int frames, unsigned rate, float masterVolume, bool mute, uint32_t queuedBytes)
{
    int setting = VitaExtras_GetFast(FAST_EXTRA_AMBIENT_VOLUME);
    float gain;

    if (stereo == NULL || frames <= 0)
        return;
    setting = setting < 0 ? 0 : (setting > 10 ? 10 : setting);
    gain = ((float)setting / 10.0f) * masterVolume;

    if (!sReady)
    {
        DeckAmbient_Init(rate, 0x5eed1234u);
        sReady = true;
    }

    if (gain > 0.0f)
    {
        if (mute)
        {
            DeckAmbient_Mute();
        }
        else
        {
            if (sFrame % UPDATE_EVERY == 0)
                UpdateTargets();
            DeckAmbient_Mix(stereo, frames, gain);

            // The bed plus the music must not exceed full scale. The stats feed the 5 s log line (debug only).
            for (int i = 0; i < frames * 2; i++)
            {
                float v = stereo[i];
                if (v > 1.0f) stereo[i] = v = 1.0f;
                else if (v < -1.0f) stereo[i] = v = -1.0f;
                sSumSq += (double)v * v;
                if (fabs(v) > sPeak) sPeak = fabs(v);
            }
            sSamples += frames * 2;
            if (queuedBytes < sQMin) sQMin = queuedBytes;
            if (queuedBytes > sQMax) sQMax = queuedBytes;
            if ((sFrame + 1) % STATS_EVERY == 0)
            {
                Log("ambient: 5s out peak %.3f rms %.4f queue min %u max %u bytes setting %d master %.2f\n", sPeak,
                         sSamples ? sqrt(sSumSq / (double)sSamples) : 0.0, (unsigned)sQMin, (unsigned)sQMax, setting,
                         (double)DeckAmbient_MasterLevel());
                sSumSq = sPeak = 0.0;
                sSamples = 0;
                sQMin = 0xffffffffu;
                sQMax = 0;
            }
        }
    }
    else
    {
        DeckAmbient_Mix(stereo, frames, 0.0f);   // hard off: no DSP, the buffer is not touched
    }
    sFrame++;
}
