#ifndef GUARD_DECK_WINDOW_H
#define GUARD_DECK_WINDOW_H
// Window mode and size (settings keys "fullscreen" and "resolution"), implemented in deck_video.c.
// No SDL include here: the game's own headers (vita_extras.c) cannot take SDL's.

#define DECK_RES_MAX 16                                     // list size incl. AUTO (row of the options menu)
extern const char *gDeckResValues[DECK_RES_MAX + 1];       // labels of the resolution row, "" past the list
void DeckVideo_Poll(void);                                  // main thread: applies a pending change, once per frame
void DeckVideo_RequestApply(void);                          // any thread: window mode or size changed
void DeckVideo_ToggleFullscreen(void);                      // main thread: F11, Alt+Enter
int DeckVideo_GetResolutionIndex(void);                     // 0 = AUTO
void DeckVideo_SetResolutionIndex(int index);
int DeckVideo_ResolutionCount(void);
const char *DeckVideo_GetResolutionText(void);              // "auto" or "WxH", for settings.txt
void DeckVideo_SetResolutionText(const char *text);
#endif
