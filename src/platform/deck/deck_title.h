#ifndef GUARD_DECK_TITLE_H
#define GUARD_DECK_TITLE_H

#include <stdbool.h>

// Deck title screen "Pokemon Bravely Emerald 2D-HD": the original composition (sky, Rayquaza, clouds, logo, banner) made richer:
// own sprites and parallax layers for the sky, Rayquaza and clouds, light rays, motes, rim light, a shine on the logo and a small
// BRAVELY 2D-HD badge. The original title state machine (src/title_screen.c) keeps running underneath: input, music, fades and
// the exit to the main menu behave exactly as before. Setting voxel_title (ORIGINAL / BRAVELY), env DECK_TITLE_ORIGINAL=1.
// Every failure logs [TITLE] and the original picture is drawn. GL (Core 0) thread only.

// Decides this frame (call once per frame, before the GBA frame is drawn): true when the title scene replaces the picture.
// While true, sdl2.c VDraw draws only the title's front layer (BG2 logo + sprites) with the backdrop keyed to magenta 0x7C1F
// and BG0 / BG1 hidden; the sky, Rayquaza and clouds come from here.
bool DeckTitle_Wanted(void);
bool DeckTitle_FrameActive(void); // the answer of the last DeckTitle_Wanted
// Draws the whole title into the current framebuffer (capture target or the letterboxed window), ortho 960x544 already set.
// frontTex: the GL texture of the keyed GBA frame (RGB565, 288 px lines); vx0..vx1 and u0..u1 map it to the screen as the plain picture would.
void DeckTitle_Draw(unsigned int frontTex, float vx0, float vx1, float u0, float u1);

#endif
