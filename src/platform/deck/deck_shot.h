#ifndef GUARD_DECK_SHOT_H
#define GUARD_DECK_SHOT_H

// DECK_SHOT="<ms>:<path.png>[,<ms>:<path.png>...]": in-engine screenshots for unattended runs.
// Times count from process start (same clock as DECK_AUTOPLAY). Call once per frame from the GL
// thread, after the frame is drawn and before the buffer swap.
void DeckShot_Frame(int winW, int winH);

#endif
