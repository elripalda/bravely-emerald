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
void DeckSway_SetChunkOffset(float x, float z, float frameX, float frameZ);
// Player bend (D3): up to DECK_SWAY_WALKERS walkers (x, z in the current frame, tile centres). Grass cards
// (weight bit 0 set) lean away from them. Call before DeckSway_Begin; n = 0 clears.
#define DECK_SWAY_WALKERS 4
void DeckSway_SetWalkers(const float (*xz)[2], int n);
// Standing tall grass (E1, voxel_grass): setting on, shaders on and the sway program built. Chunk worker safe.
bool DeckGrass_Enabled(void);
// Percent of cells that get a tuft (voxel_grass level), 0 when off. A change must rebuild the chunks (hash).
int DeckGrass_Density(void);
void DeckSway_End(void);

#endif
