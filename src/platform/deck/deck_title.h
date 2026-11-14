#ifndef GUARD_DECK_TITLE_H
#define GUARD_DECK_TITLE_H

#include <stdbool.h>

// Deck title screen "Pokemon Bravely Emerald 2D-HD": a layered 2D-HD diorama (golden hour Hoenn coast, Rayquaza, sun rays,
// clouds, sea shimmer, motes, fireflies, foreground grass, logo) drawn by our own GL code in place of the GBA picture while
// the original title state machine (src/title_screen.c) keeps running underneath: input, music, fades and the exit to the main
// menu behave exactly as before. Setting voxel_title (ORIGINAL / BRAVELY), env DECK_TITLE_ORIGINAL=1. Every failure logs
// [TITLE] and the original picture is drawn. GL (Core 0) thread only.

// true: this frame is the title and the scene should be drawn with DeckTitle_Draw instead of the GBA picture.
bool DeckTitle_Wanted(void);
// Draws the whole title into the current framebuffer (capture target or the letterboxed window), ortho 960x544 already set.
void DeckTitle_Draw(void);

#endif
