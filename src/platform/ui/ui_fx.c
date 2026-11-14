// UI polish of the new UI on the Steam Deck (voxel_uisel, voxel_uianim, voxel_uibars, voxel_uiglyphs; docs/DECK_NOTES.md
// "UI polish"). Everything here runs on Core 1 while the draw list is built. Off (and on every other platform) each call does
// nothing and the widgets draw as before.

#include <math.h>
#include <string.h>
#include "ui_internal.h"
#include "platform/vita_extras.h"
#ifdef PLATFORM_DECK
#include "../deck/deck_uifx.h"
#endif

#ifndef PLATFORM_DECK

bool UiFx_BarsOn(void) { return false; }
float UiFx_BarShown(int32_t x, int32_t y, int32_t w, float target) { (void)x; (void)y; (void)w; return target; }
uint32_t UiFx_HpColour(float pct, int which) { (void)pct; (void)which; return 0; }
void UiFx_BarGlow(int32_t x, int32_t y, int32_t w, int32_t h, float pct) { (void)x; (void)y; (void)w; (void)h; (void)pct; }
void UiFx_SelRow(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
void UiFx_SelFlush(void) {}
void UiFx_AnimReset(void) {}
void UiFx_AnimApply(bool open) { (void)open; }
void UiFx_AnimClosed(void) {}
bool UiFx_AnimClosing(void) { return false; }
int UiFx_GlyphMode(void) { return 0; }

#else

// ---------- Tuning (low, medium, high) ----------
static const float kPulse[3] = { 0.5f, 0.8f, 1.1f };      // strength of the pulsing highlight on the selected row
static const float kSlide[3] = { 0.22f, 0.30f, 0.38f };    // share of the distance the sliding trail covers a tick
static const int kAnimTicks[3] = { 6, 9, 12 };              // length of a menu fade / slide in ticks (60 per second)
static const float kAnimSlide[3] = { 6.0f, 10.0f, 14.0f }; // px a panel slides in from
static const float kBarRate[3] = { 0.16f, 0.10f, 0.07f };   // share of the gap an HP / EXP bar closes per tick

// ---------- HP / EXP bars ----------

#define BAR_SLOTS 24
static struct { int32_t x, y, w; float shown; uint32_t frame; } sBar[BAR_SLOTS];
static int sBarNext;

bool UiFx_BarsOn(void)
{
    return DeckUiFx_Level(FAST_EXTRA_VOXEL_UIBARS) >= 0;
}

// The displayed percent of the bar at (x, y, w): follows the target smoothly; a bar that was not drawn last tick (new screen,
// another list) starts at the target.
float UiFx_BarShown(int32_t x, int32_t y, int32_t w, float target)
{
    const int lvl = DeckUiFx_Level(FAST_EXTRA_VOXEL_UIBARS);
    const uint32_t f = UiDraw_Clock();
    int slot = -1;

    if (lvl < 0)
        return target;
    for (int i = 0; i < BAR_SLOTS; i++)
        if (sBar[i].w == w && sBar[i].x == x && sBar[i].y == y && f - sBar[i].frame <= 2)
        {
            slot = i;
            break;
        }
    if (slot < 0)
    {
        slot = sBarNext;
        sBarNext = (sBarNext + 1) % BAR_SLOTS;
        sBar[slot].x = x;
        sBar[slot].y = y;
        sBar[slot].w = w;
        sBar[slot].shown = target;
    }
    else if (sBar[slot].frame != f)
    {
        const float d = target - sBar[slot].shown;
        const float step = d * kBarRate[lvl];

        // at least a tenth of a percent a tick so the last bit arrives
        sBar[slot].shown += fabsf(step) < 0.15f ? (fabsf(d) < 0.15f ? d : (d > 0 ? 0.15f : -0.15f)) : step;
    }
    sBar[slot].frame = f;
    return sBar[slot].shown;
}

static uint32_t Mix(uint32_t a, uint32_t b, float t)
{
    uint32_t out = 0xFF000000u;

    for (int s = 0; s < 24; s += 8)
    {
        const float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);

        out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
    }
    return out;
}

// Green above 60 percent, blending to yellow by 35, to red by 15. which 0 = top of the gradient (lighter), 1 = bottom.
uint32_t UiFx_HpColour(float pct, int which)
{
    uint32_t c;

    if (pct >= 60.0f)
        c = UI_COL_HP_HIGH;
    else if (pct >= 35.0f)
        c = Mix(UI_COL_HP_MID, UI_COL_HP_HIGH, (pct - 35.0f) / 25.0f);
    else if (pct >= 15.0f)
        c = Mix(UI_COL_HP_LOW, UI_COL_HP_MID, (pct - 15.0f) / 20.0f);
    else
        c = UI_COL_HP_LOW;
    return which == 0 ? Mix(c, UI_RGBA(255, 255, 255, 255), 0.30f) : c;
}

// A faint red glow behind a bar at 25 percent or less, pulsing slowly.
void UiFx_BarGlow(int32_t x, int32_t y, int32_t w, int32_t h, float pct)
{
    const int lvl = DeckUiFx_Level(FAST_EXTRA_VOXEL_UIBARS);

    if (lvl < 0 || pct > 25.0f || pct <= 0.0f)
        return;
    const float k = 0.5f + 0.5f * sinf(DeckUiFx_Seconds() * 4.0f);
    const uint32_t a = (uint32_t)((14.0f + 30.0f * k) * (lvl == 0 ? 0.6f : 1.0f));

    UiDraw_Rect(x - 3, y - 3, w + 6, h + 6, UI_RGBA(255, 60, 50, a / 2));
    UiDraw_Rect(x - 1, y - 1, w + 2, h + 2, UI_RGBA(255, 70, 60, a));
}

// ---------- animated cursor ----------

static struct
{
    bool has;
    float x, y, w, h;          // where the trail is now
    float tx, ty, tw, th;      // the selected row
    uint32_t frame;
} sSel;

void UiFx_SelRow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    const int lvl = DeckUiFx_Level(FAST_EXTRA_VOXEL_UISEL);
    const uint32_t f = UiDraw_Clock();

    if (lvl < 0)
        return;
    // pulse: a white wash over the selected row, breathing
    {
        const float k = 0.5f + 0.5f * sinf(DeckUiFx_Seconds() * 4.4f);
        const uint32_t a = (uint32_t)((2.0f + 12.0f * k) * kPulse[lvl]);
        const int32_t inset = gUiTheme->selRadius > 0 ? gUiTheme->selRadius : 0;

        UiDraw_Rect(x + inset, y + 1, w - 2 * inset - (gUiTheme->chamfer ? 12 : 0), h - 4, UI_RGBA(255, 255, 255, a));
    }
    // the slide: the trail starts from the previous row when the cursor moved within the same list
    if (!sSel.has || f - sSel.frame > 3 || fabsf((float)x - sSel.tx) > 60.0f || fabsf((float)y - sSel.ty) > 300.0f)
    {
        sSel.x = (float)x; sSel.y = (float)y; sSel.w = (float)w; sSel.h = (float)h;
    }
    else if (sSel.frame != f)
    {
        const float k = kSlide[lvl];

        sSel.x += ((float)x - sSel.x) * k;
        sSel.y += ((float)y - sSel.y) * k;
        sSel.w += ((float)w - sSel.w) * k;
        sSel.h += ((float)h - sSel.h) * k;
    }
    sSel.tx = (float)x; sSel.ty = (float)y; sSel.tw = (float)w; sSel.th = (float)h;
    sSel.has = true;
    sSel.frame = f;
}

// After the screen is built: the trail drawn over the rows (translucent, theme colour), so it is seen sliding between them.
void UiFx_SelFlush(void)
{
    const int lvl = DeckUiFx_Level(FAST_EXTRA_VOXEL_UISEL);

    if (lvl < 0 || !sSel.has || UiDraw_Clock() - sSel.frame > 1)
        return;
    const float dist = fabsf(sSel.y - sSel.ty) + fabsf(sSel.x - sSel.tx);

    if (dist < 1.5f)
        return;
    float a = dist / (sSel.th > 8.0f ? sSel.th : 8.0f);

    a = a > 1.0f ? 1.0f : a;
    const uint32_t c = gUiTheme->selBot;

    UiDraw_Rect((int32_t)sSel.x, (int32_t)sSel.y, (int32_t)sSel.w, (int32_t)sSel.h - 2, (c & 0x00FFFFFFu) | ((uint32_t)(a * 70.0f) << 24));
    UiDraw_Rect((int32_t)sSel.x, (int32_t)sSel.y, (int32_t)sSel.w, 2, UI_RGBA(255, 255, 255, (uint32_t)(a * 50.0f)));
}

// ---------- menu fade and slide ----------

static int sAge = 1000;
static int sClosing;

void UiFx_AnimReset(void)
{
    sAge = 0;
    sClosing = 0;
}

// At the end of a screen's build, before the list is published: the fade and slide of an opening screen (age in ticks).
void UiFx_AnimApply(bool open)
{
    const int lvl = DeckUiFx_Level(FAST_EXTRA_VOXEL_UIANIM);

    (void)open;
    if (lvl < 0)
    {
        sAge = 1000;
        return;
    }
    if (sAge < kAnimTicks[lvl])
    {
        const float t = (float)(sAge + 1) / (float)kAnimTicks[lvl];
        const float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

        UiDraw_SetAnim(e, (1.0f - e) * kAnimSlide[lvl]);
        sAge++;
    }
    else
        UiDraw_Snapshot();   // steady: keep the picture of the menu so it can fade out when it closes
}

// The menu closed (input is already free): fade the last picture out over the next ticks.
void UiFx_AnimClosed(void)
{
    const int lvl = DeckUiFx_Level(FAST_EXTRA_VOXEL_UIANIM);

    sClosing = lvl >= 0 && UiDraw_HasSnapshot() ? kAnimTicks[lvl] : 0;
}

// While no screen is open: appends the fading picture to the list being built. True when something was added.
bool UiFx_AnimClosing(void)
{
    const int lvl = DeckUiFx_Level(FAST_EXTRA_VOXEL_UIANIM);

    if (sClosing <= 0 || lvl < 0)
        return false;
    const float t = (float)sClosing / (float)kAnimTicks[lvl];
    const float e = t * t;

    UiDraw_AppendSnapshot(e, (1.0f - e) * kAnimSlide[lvl]);
    sClosing--;
    return true;
}

// ---------- button glyphs ----------

int UiFx_GlyphMode(void)
{
    return DeckUiFx_GlyphMode();
}

#endif
