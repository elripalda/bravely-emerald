#include "deck_video.h"
#include <stdio.h>

static SDL_Window *sWindow;
static SDL_GLContext sContext;

bool DeckVideo_Init(SDL_Window **outWindow)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    sWindow = SDL_CreateWindow("Pokemon Emerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (sWindow == NULL)
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    sContext = SDL_GL_CreateContext(sWindow);
    if (sContext == NULL)
    {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    *outWindow = sWindow;
    return true;
}

void DeckVideo_Swap(void)
{
    SDL_GL_SwapWindow(sWindow);
}

void DeckVideo_SetSwapInterval(int interval)
{
    SDL_GL_SetSwapInterval(interval);
}

void DeckVideo_GetViewport(int *x, int *y, int *w, int *h)
{
    int winW, winH;
    if (sWindow == NULL)
    {
        *x = 0; *y = 0; *w = DECK_LOGICAL_WIDTH; *h = DECK_LOGICAL_HEIGHT;
        return;
    }
    SDL_GL_GetDrawableSize(sWindow, &winW, &winH);
    // Fit 960x544 (16:9) inside the window, centred.
    int fitW = winW;
    int fitH = (winW * DECK_LOGICAL_HEIGHT) / DECK_LOGICAL_WIDTH;
    if (fitH > winH)
    {
        fitH = winH;
        fitW = (winH * DECK_LOGICAL_WIDTH) / DECK_LOGICAL_HEIGHT;
    }
    *x = (winW - fitW) / 2;
    *y = (winH - fitH) / 2;
    *w = fitW;
    *h = fitH;
}

// Compat shim for the vitaGL.h header in src/platform/deck/compat/. Only the failure/success result
// is meaningful on the Deck; the vitaGL memory-size parameters have no meaning here.
int vglInitWithCustomSizes(int a, int w, int h, int c, int d, int e, int f, int g)
{
    (void)a; (void)w; (void)h; (void)c; (void)d; (void)e; (void)f; (void)g;
    SDL_Window *win = NULL;
    return DeckVideo_Init(&win) ? 1 : 0;
}
