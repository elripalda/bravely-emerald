#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "deck_reveal.h"
#include "deck_gl_shader.h"
#include "global.h"
#include "platform/vita_extras.h"

extern u8 gDeckTitlePhase;
extern u8 gDeckIntroSegment;

// Title: the main flash fires this long after the title started (the logo is on screen from about 0.3 s), or at once when
// START skipped to PRESS START. The badge flash follows the main one. The version banner slides in later (phase 2, about
// 4.6 s) and lands already pushed down.
#define TITLE_FLASH_AT 3.0
#define TITLE_BADGE_AFTER_FLIP 2.2
#define FLASH_RISE 0.14
#define FLASH_FALL 0.95
#define EFFECT_FADE_IN 1.4
// Intro scenes (gDeckIntroSegment 1..8): seconds after the scene start at which the flash fires (the middle of the scene,
// -1 = none: the Poke Ball and lightning cuts are about a second or two, a flash there would only strobe). Real lengths from
// the [REVEAL] log lines of a run: 1 Game Freak logo and leaves 18.3 s, 2 bike ride 17.4, 3 Poke Ball 0.7, 4 Groudon 2.3,
// 5 Kyogre 4.1, 6 clouds 3.0, 7 lightning 2.0, 8 Rayquaza 3.5.
static const double kSegFlash[9] = { -1.0, 9.1, 8.7, -1.0, 1.15, 2.0, 1.5, -1.0, 1.7 };

static bool sEnvRead, sEnvOff;
static int sCtx = -1;             // 0 none, 1..8 intro segment, 100 title
static double sStart, sPhase2At = -1.0, sFlipAt = -1.0, sBadgeAt = -1.0;
static double sNow;
static bool sBadgeOn;

void DeckReveal_SetBadgeFlash(bool on) { sBadgeOn = on; }

static double Now(void) { return (double)SDL_GetTicks() / 1000.0; }

bool DeckReveal_On(void)
{
    if (!sEnvRead)
    {
        const char *e = getenv("DECK_REVEAL_OFF");

        sEnvRead = true;
        sEnvOff = e != NULL && strcmp(e, "0") != 0;
        if (sEnvOff)
            DeckGl_Log("[REVEAL] off by DECK_REVEAL_OFF\n");
    }
    return !sEnvOff && VitaExtras_GetFast(FAST_EXTRA_VOXEL_REVEAL) != 0 && VitaExtras_GetFast(FAST_EXTRA_VOXEL_SHADERS) != 0;
}

void DeckReveal_Frame(void)
{
    int ctx = 0;

    sNow = Now();
    if (gDeckTitlePhase != 0)
        ctx = 100;
    else if (gDeckIntroSegment >= 1 && gDeckIntroSegment <= 8)
        ctx = gDeckIntroSegment;
    if (ctx != sCtx)
    {
        sCtx = ctx;
        sStart = sNow;
        sPhase2At = sFlipAt = sBadgeAt = -1.0;
        if (ctx != 0)
            DeckGl_Log("[REVEAL] scene %d starts at %.2f s (reveal %s)\n", ctx, sNow, DeckReveal_On() ? "on" : "off");
    }
    if (ctx == 100)
    {
        if (gDeckTitlePhase >= 2 && sPhase2At < 0.0)
            sPhase2At = sNow;
        if (sFlipAt < 0.0 && gDeckTitlePhase != 4 && (gDeckTitlePhase >= 3 || sNow - sStart >= TITLE_FLASH_AT))
        {
            sFlipAt = sNow + FLASH_RISE;
            sBadgeAt = sFlipAt + TITLE_BADGE_AFTER_FLIP;
            DeckGl_Log("[REVEAL] title flash at t=%.2f (phase %d)\n", sFlipAt - sStart, gDeckTitlePhase);
        }
    }
    else if (ctx >= 1 && sFlipAt < 0.0 && kSegFlash[ctx] >= 0.0)
    {
        sFlipAt = sStart + kSegFlash[ctx];
        DeckGl_Log("[REVEAL] intro scene %d flash planned at +%.2f s\n", ctx, sFlipAt - sStart);
    }
}

bool DeckReveal_Hd(void)
{
    if (!DeckReveal_On())
        return true;
    if (sCtx == 0)
        return true;                       // menus and the rest keep the HD-2D look as before
    return sFlipAt >= 0.0 && sNow >= sFlipAt;
}

float DeckReveal_Amount(void)
{
    double x;

    if (!DeckReveal_On())
        return 1.0f;
    if (gDeckIntroSegment == 255 && gDeckTitlePhase == 0)
        return 0.0f;                       // the copyright screen stays flat
    if (sCtx == 0)
        return 1.0f;
    if (sFlipAt < 0.0 || sNow < sFlipAt)
        return 0.0f;
    x = (sNow - sFlipAt) / EFFECT_FADE_IN;
    x = x > 1.0 ? 1.0 : x;
    return (float)(x * x * (3.0 - 2.0 * x));
}

static float Env(double x)
{
    if (x < -FLASH_RISE)
        return 0.0f;
    if (x < 0.0)
    {
        const double r = (x + FLASH_RISE) / FLASH_RISE;

        return (float)(r * r);
    }
    if (x > FLASH_FALL)
        return 0.0f;
    {
        const double f = 1.0 - x / FLASH_FALL;

        return (float)(f * f * f);
    }
}

float DeckReveal_Flash(void)
{
    float a = 0.0f, b;

    if (!DeckReveal_On() || sCtx == 0)
        return 0.0f;
    if (sFlipAt >= 0.0)
        a = Env(sNow - sFlipAt);
    if (sBadgeAt >= 0.0 && sBadgeOn)
    {
        b = 0.55f * Env((sNow - sBadgeAt) * 1.5);
        a = a > b ? a : b;
    }
    return a;
}

float DeckReveal_SinceFlip(void)
{
    if (!DeckReveal_On() || sCtx != 100)
        return 1000.0f;
    return sFlipAt < 0.0 ? -1000.0f : (float)(sNow - sFlipAt);
}

float DeckReveal_SinceBadge(void)
{
    if (!DeckReveal_On() || sCtx != 100)
        return 1000.0f;
    return sBadgeAt < 0.0 ? -1000.0f : (float)(sNow - sBadgeAt);
}

float DeckReveal_Time(void)
{
    return (float)(sNow - sStart);
}

void DeckReveal_DrawFlash(void)
{
    const float a = DeckReveal_Flash();

    if (a <= 0.002f)
        return;
    DeckGl_UseProgram(NULL);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 0.99f, 0.95f, a > 1.0f ? 1.0f : a);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(960.0f, 0.0f);
    glVertex2f(960.0f, 544.0f);
    glVertex2f(0.0f, 544.0f);
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}
