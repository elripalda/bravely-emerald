#ifndef GUARD_DECK_VIDEO_H
#define GUARD_DECK_VIDEO_H
#include <stdbool.h>
#include <SDL2/SDL.h>

#define DECK_LOGICAL_WIDTH  960
#define DECK_LOGICAL_HEIGHT 544

bool DeckVideo_Init(SDL_Window **outWindow);               // window + GL context, vsync on
void DeckVideo_Swap(void);                                  // SDL_GL_SwapWindow
void DeckVideo_SetSwapInterval(int interval);               // 0, 1 or 2
// Letterboxed 960x544 viewport inside the window, in window pixels.
void DeckVideo_GetViewport(int *x, int *y, int *w, int *h);
SDL_Window *DeckVideo_GetWindow(void);                    // may be called from any thread
void DeckGlViewport(int x, int y, int w, int h);           // letterboxing glViewport (see deck_video.c)
#endif
