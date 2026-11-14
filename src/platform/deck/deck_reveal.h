#ifndef GUARD_DECK_REVEAL_H
#define GUARD_DECK_REVEAL_H

#include <stdbool.h>

// Reveal flash (Deck): the title screen and every intro scene start as the plain flat 2D picture; after a short beat a white
// flash fires and the HD-2D look (voxel_flatfx effects, the HD-2D title scene) takes over and fades in; the title then drops in
// BRAVELY, pushes EMERALD VERSION down and reveals the HD-2D crest with a second, smaller flash. Setting voxel_reveal
// (OFF / ON, default ON), env DECK_REVEAL_OFF=1: off = the HD-2D look from the first frame, no flash, as before.
// Time is wall clock (continuous), scenes are detected from gDeckTitlePhase (title_screen.c) and gDeckIntroSegment (intro.c).
// GL (Core 0) thread only.

void DeckReveal_Frame(void);        // once per 2D frame (sdl2.c VDraw), before DeckTitle_Wanted and the flat pass
bool DeckReveal_On(void);           // the reveal is active (setting on, env not set)
bool DeckReveal_Hd(void);           // the current scene is past its flash: the HD-2D look may be drawn
float DeckReveal_Amount(void);      // strength of the HD-2D effects, 0 flat .. 1 full (eased after the flash)
float DeckReveal_Flash(void);       // white flash alpha right now
float DeckReveal_SinceFlip(void);   // seconds since the main flash peak (title drop-in clock); 1000 when the reveal is off
float DeckReveal_SinceBadge(void);  // seconds since the badge flash peak; 1000 when the reveal is off, negative before
float DeckReveal_Time(void);        // seconds since the current scene started
void DeckReveal_DrawFlash(void);    // adds the white flash over the frame (fixed pipeline, ortho 960x544 set)

#endif
