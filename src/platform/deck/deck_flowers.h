#ifndef GUARD_DECK_FLOWERS_H
#define GUARD_DECK_FLOWERS_H

#include <stdint.h>

// Standing flowers (phase 4, E2): small procedural pixel-art flower cards painted into the free rows of the
// tree atlas (py 112..123, x 0..31) once when it is loaded. 4 variants of 8x12 texels side by side.
#define DECK_FLOWER_ATLAS_X 0
#define DECK_FLOWER_ATLAS_Y 112
#define DECK_FLOWER_W 8
#define DECK_FLOWER_H 12
#define DECK_FLOWER_VARIANTS 4

// Writes the sprites into `out` (DECK_FLOWER_VARIANTS * DECK_FLOWER_W wide, DECK_FLOWER_H rows, top row first),
// RGBA5551 (R bits 15..11, A bit 0); the caller grades them like the rest of the atlas and copies them in.
void DeckFlowers_Sprites(uint16_t *out);

#endif
