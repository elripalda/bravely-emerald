#ifndef GUARD_DECK_GL_SHADER_H
#define GUARD_DECK_GL_SHADER_H

#include <stdbool.h>
#include <GL/gl.h>
#include <GL/glext.h>

// GLSL 1.20 programs on the compatibility context (Deck only). GL 2.0 entry points come from
// SDL_GL_GetProcAddress; nothing here relies on GL_GLEXT_PROTOTYPES for them.
// Logs go to userdata/gl_errors.log and stderr. GL (main) thread only.

typedef struct
{
    GLuint program;      // 0 when the build failed
    const char *name;
    GLint uTime, uWind;  // -1 when the program does not use them
} DeckGlProgram;

typedef enum
{
    DECK_GL_DEPTH_NONE = 0,
    DECK_GL_DEPTH_TEXTURE24,       // depth texture (readable by later DoF work)
    DECK_GL_DEPTH_RENDERBUFFER24,
} DeckGlDepth;

typedef struct
{
    GLuint fbo, color, depthTex, depthRb;
    int w, h;
    DeckGlDepth depth;
    bool complete;
} DeckGlTarget;

void DeckGl_Log(const char *fmt, ...);
bool DeckGl_Init(void);            // once per context; false: no GLSL 1.20 or an entry point is missing
bool DeckGl_SelfTest(void);        // builds and deletes a trivial program, logs the result
bool DeckGl_BuildProgram(DeckGlProgram *out, const char *name, const char *vs, const char *fs);
void DeckGl_DeleteProgram(DeckGlProgram *p);
void DeckGl_UseProgram(const DeckGlProgram *p); // NULL: glUseProgram(0); else also sets uTime/uWind
GLint DeckGl_Uniform(const DeckGlProgram *p, const char *name);
void DeckGl_Set1i(GLint loc, int v);
void DeckGl_Set1f(GLint loc, float v);
void DeckGl_Set2f(GLint loc, float a, float b);
void DeckGl_Set3f(GLint loc, float a, float b, float c);
void DeckGl_Set4f(GLint loc, float a, float b, float c, float d);
void DeckGl_ActiveTexture(GLenum unit);
// Per-frame values for every program that declares uTime / uWind (wind = dirX, dirZ, strength).
void DeckGl_SetFrameUniforms(float timeSeconds, float windX, float windZ, float windStrength);

// RGBA8 colour target with optional depth; logs glCheckFramebufferStatus; restores the binding.
bool DeckGl_CreateTarget(DeckGlTarget *t, int w, int h, DeckGlDepth depth, GLint filter);
void DeckGl_DeleteTarget(DeckGlTarget *t);

// GL_TIME_ELAPSED timer (GL_ARB_timer_query), two queries in turn.
bool DeckGl_TimerInit(void);
void DeckGl_TimerShutdown(void);
void DeckGl_TimerBegin(void);
void DeckGl_TimerEnd(void);
double DeckGl_TimerLastMs(void);   // newest finished result, -1 if none

#endif
