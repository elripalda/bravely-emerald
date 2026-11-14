#ifndef DECK_AMBIENT_H
#define DECK_AMBIENT_H

// Procedural ambient bed for the Steam Deck port (spec 3.h, H3 with procedural layers only).
// Pure DSP, no game headers: the game state to targets mapping lives in deck_ambient_game.c.
// Everything runs on the game thread (the caller of Platform_QueueAudio); nothing here is thread safe.

typedef struct
{
    float wind;     // 0..1 level of each layer; the mixer slews towards these (about 2.5 s)
    float birds;
    float crickets;
    float water;    // waves lapping
    float rain;
    float cave;     // low room tone and drips
    float master;   // 0..1, 1 s fade; 0 = whole bed fades out (indoors, battles, menus)
} DeckAmbientTargets;

// rate in Hz (48000 in the game). Safe to call again (resets the state).
void DeckAmbient_Init(unsigned rate, unsigned seed);
void DeckAmbient_SetTargets(const DeckAmbientTargets *targets);
// A thunder rumble starts after a random 0.3 to 1.5 s delay.
void DeckAmbient_Thunder(void);
// Rate multiplier of the one-shot events (bird calls, cave drips): 1 = the original rate, 0.6 = sparse. The beds are untouched.
void DeckAmbient_SetDensity(float density);
// Adds the bed into an interleaved stereo float buffer. gain <= 0 returns at once (the hard off switch:
// no DSP, no state change). The number of frames is never changed.
void DeckAmbient_Mix(float *stereo, int frames, float gain);
// Fast forward and similar: forget everything audible at once; the bed fades back in over 1 s.
void DeckAmbient_Mute(void);
float DeckAmbient_MasterLevel(void);

#endif
