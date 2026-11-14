#ifndef DECK_AMBIENT_GAME_H
#define DECK_AMBIENT_GAME_H

#include <stdbool.h>
#include <stdint.h>

// Game thread, once per game frame from Platform_QueueAudio (after the master volume, before the DC blocker).
// Adds the ambient bed into `stereo` (interleaved float, `frames` frames). `masterVolume` is the 0..1 master
// volume, `mute` is true in fast forward, `queuedBytes` is the SDL queue size (only logged).
// The frame count is never changed. ambient_volume 0 does nothing at all.
void DeckAmbient_Frame(float *stereo, int frames, unsigned rate, float masterVolume, bool mute, uint32_t queuedBytes);

#endif
