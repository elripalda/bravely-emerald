#include "deck_shot.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <GL/gl.h>
#include "vita_platform.h"

#define SHOT_MAX 16
#define SHOT_PATH_MAX 256

static struct { uint32_t ms; char path[SHOT_PATH_MAX]; bool done; } sShot[SHOT_MAX];
static int sCount = -1; // -1 = env not parsed yet

static void ShotLog(const char *fmt, const char *a, int b)
{
    FILE *f = fopen(VITA_DATA_DIR "debug.log", "a");
    if (f != NULL)
    {
        fprintf(f, fmt, a, b);
        fclose(f);
    }
}

static void ShotLoad(void)
{
    const char *env = getenv("DECK_SHOT");
    char *copy, *save = NULL, *tok;

    sCount = 0;
    if (env == NULL || env[0] == '\0')
        return;
    copy = strdup(env);
    if (copy == NULL)
        return;
    for (tok = strtok_r(copy, ",", &save); tok != NULL && sCount < SHOT_MAX; tok = strtok_r(NULL, ",", &save))
    {
        char *colon = strchr(tok, ':');
        if (colon == NULL || strlen(colon + 1) >= SHOT_PATH_MAX)
            continue;
        sShot[sCount].ms = (uint32_t)strtoul(tok, NULL, 10);
        strcpy(sShot[sCount].path, colon + 1);
        sCount++;
    }
    free(copy);
    ShotLog("[SHOT] %s: entries=%d\n", "DECK_SHOT", sCount);
}

static void ShotSave(const char *path, int w, int h)
{
    uint8_t *px = malloc((size_t)w * (size_t)h * 4), *flip = malloc((size_t)w * (size_t)h * 4);
    if (px == NULL || flip == NULL)
    {
        free(px);
        free(flip);
        return;
    }
    GLint prevAlign = 4, prevRead = GL_BACK, prevFbo = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlign);
    glGetIntegerv(GL_READ_BUFFER, &prevRead);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    if (prevFbo != 0)
        glBindFramebuffer(GL_FRAMEBUFFER, 0); // the window back buffer is wanted, not an offscreen target
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glReadBuffer((GLenum)prevRead);
    glPixelStorei(GL_PACK_ALIGNMENT, prevAlign);
    if (prevFbo != 0)
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    for (int y = 0; y < h; y++) // GL rows run bottom to top
        memcpy(flip + (size_t)y * w * 4, px + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    for (size_t i = 3; i < (size_t)w * h * 4; i += 4)
        flip[i] = 255; // the window back buffer may hold alpha 0
    SDL_Surface *s = SDL_CreateRGBSurfaceFrom(flip, w, h, 32, w * 4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    int rc = -1;
    if (s != NULL)
    {
        rc = IMG_SavePNG(s, path);
        SDL_FreeSurface(s);
    }
    ShotLog("[SHOT] saved %s: rc=%d\n", path, rc);
    free(px);
    free(flip);
}

void DeckShot_Frame(int winW, int winH)
{
    if (sCount < 0)
        ShotLoad();
    if (sCount == 0)
        return;
    const uint32_t now = (uint32_t)VitaPlatform_GetMilliseconds();
    for (int i = 0; i < sCount; i++)
    {
        if (!sShot[i].done && now >= sShot[i].ms)
        {
            sShot[i].done = true;
            ShotSave(sShot[i].path, winW, winH);
        }
    }
}
