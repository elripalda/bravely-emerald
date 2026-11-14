#include "deck_uiglow.h"
#include "deck_gl_shader.h"
#include "deck_levels.h"
#include "platform/vita_extras.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Tuning: by level (low, medium, high) the number of taps, the radius of the rings in pixels, the opacity of each tap
// and the downward shift of the whole halo (a short drop shadow offset).
static const int kTaps[3] = { 8, 12, 16 };
static const float kRing1[3] = { 2.0f, 1.8f, 2.0f };
static const float kRing2[3] = { 0.0f, 3.6f, 4.2f };
static const float kAlpha[3] = { 0.12f, 0.10f, 0.085f };
static const float kShiftX = 1.0f;
static const float kShiftY = 2.5f;

bool DeckUiGlow_Get(DeckUiGlow *g)
{
    static int sEnv = -1;
    static int sLogged = -2;
    int lvl;

    if (sEnv < 0)
    {
        const char *e = getenv("DECK_UIGLOW_OFF");

        sEnv = e != NULL && strcmp(e, "0") != 0;
        if (sEnv)
            DeckGl_Log("[UIGLOW] off by DECK_UIGLOW_OFF\n");
    }
    if (sEnv || VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) == 0)
        return false;
    lvl = DeckLevel(FAST_EXTRA_VOXEL_UIGLOW);
    if (lvl != sLogged)
    {
        DeckGl_Log("[UIGLOW] level %d (-1 off, 0 low .. 2 high)\n", lvl);
        sLogged = lvl;
    }
    if (lvl < 0)
        return false;
    g->count = kTaps[lvl];
    g->alpha = kAlpha[lvl];
    {
        // inner ring of 8 (or all taps at the one ring at LOW), the rest on an outer ring offset by half a step
        const int inner = lvl == 0 ? kTaps[lvl] : 8;

        for (int i = 0; i < g->count; i++)
        {
            const bool in1 = i < inner;
            const int n = in1 ? inner : g->count - inner;
            const int k = in1 ? i : i - inner;
            const float a = 6.2831853f * ((float)k + (in1 ? 0.0f : 0.5f)) / (float)n;
            const float r = in1 ? kRing1[lvl] : kRing2[lvl];

            g->dx[i] = cosf(a) * r + kShiftX;
            g->dy[i] = sinf(a) * r + kShiftY;
        }
    }
    return true;
}
