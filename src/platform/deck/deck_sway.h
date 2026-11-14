#ifndef GUARD_DECK_SWAY_H
#define GUARD_DECK_SWAY_H

#include <stdbool.h>

// Wind sway of tree crowns and long grass (phase 2, D1). The chunk builder packs a sway weight into the
// tree vertices' alpha (255 = still, 255 - 190 = full); this vertex shader moves them along the wind.
// GL (Core 0) thread only. Any failure (no shaders, compile error, voxel_sway=0) leaves the fixed-function
// tree pass untouched.

void DeckSway_Init(void);
void DeckSway_Shutdown(void);
// true: the sway program is bound (texture unit 0 sampled). Call before drawing the tree chunks.
bool DeckSway_Begin(void);
// World-frame offset of the chunk about to be drawn: its draw offset minus all map-crossing shifts so far,
// so the wave phase of a vertex does not change when the world is re-based.
void DeckSway_SetChunkOffset(float x, float z);
void DeckSway_End(void);

#endif
