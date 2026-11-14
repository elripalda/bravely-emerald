#ifndef GUARD_DECK_LEVELS_H
#define GUARD_DECK_LEVELS_H

#include "platform/vita_extras.h"

// Level keys of the Deck effects. Stored value: 0 OFF, 1 AUTO (follow voxel_quality; this is what an old "on" meant),
// 2 LOW, 3 MEDIUM, 4 HIGH. Anything else counts as AUTO. voxel_shaders stays the master switch (checked by each reader).

// -1 = off, else 0 low, 1 medium, 2 high (AUTO resolved against the preset)
static inline int DeckLevel(FastExtraId id)
{
    const int v = VitaExtras_GetFast(id);
    int q;

    if (v == 0)
        return -1;
    if (v >= 2 && v <= 4)
        return v - 2;
    q = VitaExtras_GetFast(FAST_EXTRA_VOXEL_QUALITY);
    return (q < 0 || q > 2) ? 1 : q;
}

// 0 when off, else the factor of the resolved level. MEDIUM is the look before the levels existed.
static inline float DeckLevelPick(FastExtraId id, float low, float medium, float high)
{
    const int l = DeckLevel(id);

    return l < 0 ? 0.0f : (l == 0 ? low : (l == 1 ? medium : high));
}

#endif
