#ifndef GUARD_DECK_UIGLOW_H
#define GUARD_DECK_UIGLOW_H

#include <stdbool.h>

// UI depth glow (voxel_uiglow): a soft dark halo under the UI (menus, text boxes, HP bars) so it looks raised above the
// game. The UI is drawn a few times first, black, shifted around a short offset, at low opacity (fixed pipeline, no
// shader, no blur of the UI itself); the normal draw follows on top. Called only from the UI passes: the 3D scene is
// untouched. Env DECK_UIGLOW_OFF=1 switches it off for A/B shots.

#define DECK_UIGLOW_MAX 16

typedef struct
{
    int count;                          // taps (0 = off)
    float dx[DECK_UIGLOW_MAX];          // offsets in pixels of the logical 960x544 picture
    float dy[DECK_UIGLOW_MAX];
    float alpha;                        // opacity of each tap
} DeckUiGlow;

// false when the setting is off, shaders are off or DECK_UIGLOW_OFF is set (g untouched).
bool DeckUiGlow_Get(DeckUiGlow *g);

#endif
