#include "deck_video.h"
#include <stdio.h>
#include <GL/gl.h>
#include <GL/glext.h>

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
    // Black letterbox bars: the game clears with its own colour, but only inside the scissor
    // rect set by DeckGlViewport, so the bars keep what is cleared here.
    int winW, winH;
    GLint vp[4];
    GLfloat cc[4];
    SDL_GL_GetDrawableSize(sWindow, &winW, &winH);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, winW, winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(cc[0], cc[1], cc[2], cc[3]);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

SDL_Window *DeckVideo_GetWindow(void)
{
    return sWindow;
}

// Replaces glViewport in game code (macro in compat/vitaGL.h). The game draws its 960x544 picture
// with glViewport(0,0,960,544) on the default framebuffer: map that into the letterboxed rect.
// Any other call (FBO passes, other sizes) is passed through unchanged.
void DeckGlViewport(int x, int y, int w, int h)
{
    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    if (fbo == 0 && sWindow != NULL && x == 0 && y == 0 && w == DECK_LOGICAL_WIDTH && h == DECK_LOGICAL_HEIGHT)
    {
        int vx, vy, vw, vh, winW, winH;
        DeckVideo_GetViewport(&vx, &vy, &vw, &vh);
        SDL_GL_GetDrawableSize(sWindow, &winW, &winH);
        const int glY = winH - (vy + vh);   // GL origin is bottom-left
        glViewport(vx, glY, vw, vh);
        glScissor(vx, glY, vw, vh);
        glEnable(GL_SCISSOR_TEST);
        return;
    }
    glDisable(GL_SCISSOR_TEST);
    glViewport(x, y, w, h);
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
