#ifndef GUARD_DECK_UIFX_H
#define GUARD_DECK_UIFX_H

#include <stdbool.h>
#include <stdint.h>
#include "platform/vita_extras.h"

// UI polish (Deck only, docs/DECK_NOTES.md "UI polish"). Settings: voxel_uiglass, voxel_uitint, voxel_uitext,
// voxel_uisel, voxel_uianim, voxel_uibars, voxel_uiglyphs, voxel_uilayout. Every one has OFF = the UI as it was.
// Env A/B switches (read once): DECK_UIFX_OFF=1 switches all of them off, DECK_UIGLASS_OFF=1 only the glass and tint
// pass (the GL part). Logs are [UIFX] lines in gl_errors.log.
//
// Two halves: the helpers below run on Core 1 (the thread that builds the UI list) or anywhere; the GL pass (glass +
// tint) runs on Core 0 from Ui_Render and the GBA overlay.

// One frosted panel, in the logical 960x544 picture.
typedef struct
{
    float x, y, w, h;
    float radius;          // corner size (px)
    uint8_t chamfer;       // corners cut diagonally instead of rounded
    uint32_t base;         // 0xAABBGGRR: the panel's own fill colour (the glass is pulled toward it for readability)
    uint32_t colA, colB;   // tint gradient colours (alpha unused)
    uint32_t firstVert;    // vertices of the list drawn before this panel (z order); 0 for the overlay
} DeckUiPanel;

#define DECK_UI_MAX_PANELS 28

// ---- levels (any thread) ----
// -1 = off, else 0 low, 1 medium, 2 high (AUTO resolves against voxel_quality). Off when DECK_UIFX_OFF is set.
int DeckUiFx_Level(FastExtraId id);
// The GL pass (glass or tint) wants to run: needs the shader master switch and at least one of the two levels on.
bool DeckUiFx_PanelsActive(void);
// Alpha multiplier of the panel fill (255 = opaque) while the glass or the tint is on; 255 when both are off.
int DeckUiFx_FillAlpha(void);
// Text clarity: 0 none, 1 soft shadow, 2 outline (4 directions) + shadow, 3 outline (8 directions) + shadow.
int DeckUiFx_TextMode(void);
// Seconds since start, continuous (the glass and the animations never step).
float DeckUiFx_Seconds(void);
// Tint gradient colours for a theme: the two theme colours shifted by the time of day (warm at dusk, cool at night).
void DeckUiFx_TintColours(uint32_t themeA, uint32_t themeB, uint32_t *outA, uint32_t *outB);

// ---- GL pass (Core 0, GL thread) ----
// Before the first panel of a frame: copies the picture, blurs it (glass level) and sets up the state. Returns false
// when nothing will be drawn (then do not call Panel / End).
bool DeckUiFx_Begin(const DeckUiPanel *panels, int count);
// Draws one panel's glass / tint, over what is on screen. fade 0..1, slideY in logical px (the panel is moved with the list).
void DeckUiFx_Panel(const DeckUiPanel *p, float fade, float slideY);
void DeckUiFx_End(void);

#endif
