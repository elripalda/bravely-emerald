#ifndef GUARD_DECK_FLATFX_H
#define GUARD_DECK_FLATFX_H

#include <stdbool.h>

// Flat 2D post pass (Deck only): the plain GBA picture of the intro, title screen and menus is captured into a
// window-sized RGBA8 target and composited back with bloom, a soft glow, a colour grade, vignette, film grain and a
// gentle light pulse. No depth effects. The base picture is copied 1:1 with NEAREST (pixel art stays sharp), bloom and
// glow are additive. Setting voxel_flatfx (OFF / AUTO / LOW / MEDIUM / HIGH), env DECK_FLATFX_OFF=1. GL (Core 0) thread only.
// Any failure ([FLATFX] log) turns the pass off for the run and the picture goes straight to the window as before.

// Called by RenderGameTexture (sdl2.c) after its clear, before the picture quad. true: framebuffer, viewport and scissor
// now point at the capture target (draw the picture with the usual 960x544 ortho), call DeckFlat_End afterwards.
bool DeckFlat_Begin(void);
// Composites the capture into framebuffer 0 (letterboxed viewport) and restores the state RenderGameTexture leaves.
void DeckFlat_End(void);
// Resolved level: -1 off, 0 low, 1 medium, 2 high (not checked against failures).
int DeckFlat_Level(void);

#endif
