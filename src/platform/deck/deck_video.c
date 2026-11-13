#include "deck_video.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "vita_platform.h"
#include <GL/gl.h>
#include <GL/glext.h>

static SDL_Window *sWindow;
static SDL_GLContext sContext;

// GL diagnostics, enabled by the environment variable DECK_GL_DEBUG (anything but "0").
static bool sGlDebug;
static unsigned sFrame;
#define GLERR_LOG_PATH VITA_DATA_DIR "gl_errors.log"
#define GLERR_LOG_MAX 20 // lines printed per (where, code); later hits are only counted
static struct { const char *where; GLenum code; unsigned count; } sErrTable[64];

// Appends one line to userdata/debug.log and flushes at once (the VITA_LOG stream is buffered).
static void DeckDebugLog(const char *fmt, ...)
{
    FILE *f = fopen(VITA_DATA_DIR "debug.log", "a");
    va_list ap;
    if (f == NULL)
        return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

static const char *GlErrorName(GLenum e)
{
    switch (e)
    {
    case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
    case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
    case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
    case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    default: return "unknown";
    }
}

static void GlErrLog(const char *fmt, ...)
{
    FILE *f = fopen(GLERR_LOG_PATH, "a");
    va_list ap;
    if (f == NULL)
        return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

// Drains glGetError; each distinct (where, code) is logged GLERR_LOG_MAX times, then counted.
// Main (GL context) thread only. One branch when DECK_GL_DEBUG is off.
void DeckGl_CheckErrors(const char *where)
{
    GLenum e;
    if (!sGlDebug)
        return;
    while ((e = glGetError()) != GL_NO_ERROR)
    {
        int i;
        for (i = 0; i < 64 && sErrTable[i].where != NULL; i++)
            if (sErrTable[i].code == e && strcmp(sErrTable[i].where, where) == 0)
                break;
        if (i == 64)
            continue; // table full
        if (sErrTable[i].where == NULL)
        {
            sErrTable[i].where = where;
            sErrTable[i].code = e;
        }
        sErrTable[i].count++;
        if (sErrTable[i].count <= GLERR_LOG_MAX)
        {
            fprintf(stderr, "GLERR frame=%u where=%s code=0x%04x (%s)\n", sFrame, where, (unsigned)e, GlErrorName(e));
            GlErrLog("GLERR frame=%u where=%s code=0x%04x (%s) count=%u\n", sFrame, where, (unsigned)e, GlErrorName(e), sErrTable[i].count);
        }
    }
}

// Logs glCheckFramebufferStatus for each FBO to userdata/debug.log (always) and gl_errors.log (debug).
void DeckGlCheckFbos(unsigned int diorama, unsigned int scratch)
{
    const unsigned int fbos[2] = { diorama, scratch };
    const char *names[2] = { "diorama", "scratch" };
    GLint prev = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    for (int i = 0; i < 2; i++)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        DeckDebugLog("[GL] FBO %s (id %u): status 0x%04x %s\n", names[i], fbos[i], (unsigned)st,
                     st == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE" : "INCOMPLETE");
        if (sGlDebug)
            GlErrLog("FBO %s (id %u): status 0x%04x %s\n", names[i], fbos[i], (unsigned)st,
                     st == GL_FRAMEBUFFER_COMPLETE ? "COMPLETE" : "INCOMPLETE");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
}

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
    DeckVideo_SetSwapInterval(1);
    {
        const char *dbg = getenv("DECK_GL_DEBUG");
        sGlDebug = (dbg != NULL && strcmp(dbg, "0") != 0);
        GLint depthBits = -1;
        int attrDepth = -1;
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        glGetIntegerv(GL_DEPTH_BITS, &depthBits);
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &attrDepth);
        if (sGlDebug)
            remove(GLERR_LOG_PATH);
        DeckDebugLog("[GL] vendor=%s | renderer=%s | version=%s | glsl=%s | vblank_mode=%s\n[GL] GL_DEPTH_BITS=%d (SDL depth attr %d) | FBO ext: ARB=%d EXT=%d | DECK_GL_DEBUG=%d\n",
                     (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_RENDERER),
                     (const char *)glGetString(GL_VERSION),
                     (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION),
                     getenv("vblank_mode") ? getenv("vblank_mode") : "unset", (int)depthBits, attrDepth,
                     ext != NULL && strstr(ext, "GL_ARB_framebuffer_object") != NULL,
                     ext != NULL && strstr(ext, "GL_EXT_framebuffer_object") != NULL, (int)sGlDebug);
    }
    *outWindow = sWindow;
    return true;
}

void DeckVideo_Swap(void)
{
    sFrame++;
    DeckGl_CheckErrors("swap");
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

// sdl2.c asks for the interval every 3D frame (sdl2.c:836, eglSwapInterval -> here). Only a change
// reaches SDL; each change is logged with SDL's answer, so the log shows whether vsync took.
static int sSwapInterval = -1000;

void DeckVideo_SetSwapInterval(int interval)
{
    if (interval == sSwapInterval)
        return;
    sSwapInterval = interval; // also on failure, so a refused value is logged once, not per frame
    const int ret = SDL_GL_SetSwapInterval(interval);
    DeckDebugLog("[GL] swap interval %d: SDL_GL_SetSwapInterval=%d (%s) | SDL_GL_GetSwapInterval=%d | video driver %s\n",
                 interval, ret, ret == 0 ? "ok" : SDL_GetError(), SDL_GL_GetSwapInterval(),
                 SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
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
