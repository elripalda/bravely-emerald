#include "deck_gl_shader.h"
#include <SDL2/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vita_platform.h"

#define DECK_GL_LOG_PATH VITA_DATA_DIR "gl_errors.log"
#define DECK_GL_LOG_MAX 1000

static struct
{
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
    PFNGLDELETESHADERPROC DeleteShader;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
    PFNGLUNIFORM1IPROC Uniform1i;
    PFNGLUNIFORM1FPROC Uniform1f;
    PFNGLUNIFORM2FPROC Uniform2f;
    PFNGLUNIFORM3FPROC Uniform3f;
    PFNGLUNIFORM4FPROC Uniform4f;
    PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;
    PFNGLACTIVETEXTUREPROC ActiveTexture;
    PFNGLGENQUERIESPROC GenQueries;
    PFNGLDELETEQUERIESPROC DeleteQueries;
    PFNGLBEGINQUERYPROC BeginQuery;
    PFNGLENDQUERYPROC EndQuery;
    PFNGLGETQUERYOBJECTIVPROC GetQueryObjectiv;
    PFNGLGETQUERYOBJECTUI64VPROC GetQueryObjectui64v;
} sGl;

static bool sLoaded, sAvailable;
static float sTime, sWind[3];

static GLuint sTimerQ[2];
static bool sTimerOn, sTimerUsed[2], sTimerActive;
static unsigned sTimerIdx;
static double sTimerLastMs = -1.0;

void DeckGl_Log(const char *fmt, ...)
{
    char line[1536];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    static unsigned sLogCount;
    // gl_errors.log must not grow without bound when nothing resets it: cap the lines per run
    if (sLogCount > DECK_GL_LOG_MAX)
        return;
    if (++sLogCount > DECK_GL_LOG_MAX)
        snprintf(line, sizeof(line), "[SHADER] log capped at %d lines, later messages dropped\n", DECK_GL_LOG_MAX);
    fputs(line, stderr);
    FILE *f = fopen(DECK_GL_LOG_PATH, "a");
    if (f != NULL)
    {
        fputs(line, f);
        fclose(f);
    }
}

// glXGetProcAddress can return non-NULL for functions the driver lacks, so a non-NULL pointer is
// necessary, not sufficient: DeckGl_SelfTest proves the path by compiling and linking.
#define DECK_GL_LOAD(field, name)                                              \
    do                                                                         \
    {                                                                          \
        sGl.field = (__typeof__(sGl.field))SDL_GL_GetProcAddress(name);        \
        if (sGl.field == NULL)                                                 \
        {                                                                      \
            DeckGl_Log("[SHADER] missing entry point %s\n", name);             \
            ok = false;                                                        \
        }                                                                      \
    } while (0)

bool DeckGl_Init(void)
{
    if (sLoaded)
        return sAvailable;
    sLoaded = true;

    const char *glsl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    int major = 0, minor = 0;
    if (glsl == NULL || sscanf(glsl, "%d.%d", &major, &minor) != 2 || major * 100 + minor < 120)
    {
        DeckGl_Log("[SHADER] GLSL 1.20 not available (GL_SHADING_LANGUAGE_VERSION=%s)\n", glsl ? glsl : "NULL");
        return false;
    }

    bool ok = true;
    DECK_GL_LOAD(CreateShader, "glCreateShader");
    DECK_GL_LOAD(ShaderSource, "glShaderSource");
    DECK_GL_LOAD(CompileShader, "glCompileShader");
    DECK_GL_LOAD(GetShaderiv, "glGetShaderiv");
    DECK_GL_LOAD(GetShaderInfoLog, "glGetShaderInfoLog");
    DECK_GL_LOAD(DeleteShader, "glDeleteShader");
    DECK_GL_LOAD(CreateProgram, "glCreateProgram");
    DECK_GL_LOAD(AttachShader, "glAttachShader");
    DECK_GL_LOAD(LinkProgram, "glLinkProgram");
    DECK_GL_LOAD(GetProgramiv, "glGetProgramiv");
    DECK_GL_LOAD(GetProgramInfoLog, "glGetProgramInfoLog");
    DECK_GL_LOAD(DeleteProgram, "glDeleteProgram");
    DECK_GL_LOAD(UseProgram, "glUseProgram");
    DECK_GL_LOAD(GetUniformLocation, "glGetUniformLocation");
    DECK_GL_LOAD(Uniform1i, "glUniform1i");
    DECK_GL_LOAD(Uniform1f, "glUniform1f");
    DECK_GL_LOAD(Uniform2f, "glUniform2f");
    DECK_GL_LOAD(Uniform3f, "glUniform3f");
    DECK_GL_LOAD(Uniform4f, "glUniform4f");
    DECK_GL_LOAD(UniformMatrix4fv, "glUniformMatrix4fv");
    DECK_GL_LOAD(ActiveTexture, "glActiveTexture");
    sAvailable = ok;
    DeckGl_Log("[SHADER] GL_VERSION=%s | GLSL=%s | GL 2.0 entry points %s\n",
               (const char *)glGetString(GL_VERSION), glsl, ok ? "loaded" : "MISSING");
    return ok;
}

static GLuint CompileStage(GLenum type, const char *name, const char *src)
{
    GLint status = 0;
    const GLuint s = sGl.CreateShader(type);
    sGl.ShaderSource(s, 1, &src, NULL);
    sGl.CompileShader(s);
    sGl.GetShaderiv(s, GL_COMPILE_STATUS, &status);
    if (!status)
    {
        char log[1024];
        GLsizei n = 0;
        sGl.GetShaderInfoLog(s, (GLsizei)sizeof(log), &n, log);
        DeckGl_Log("[SHADER] %s %s compile FAILED:\n%.*s\n", name,
                   type == GL_VERTEX_SHADER ? "vertex" : "fragment", (int)n, log);
        sGl.DeleteShader(s);
        return 0;
    }
    return s;
}

bool DeckGl_BuildProgram(DeckGlProgram *out, const char *name, const char *vs, const char *fs)
{
    memset(out, 0, sizeof(*out));
    out->name = name;
    out->uTime = -1;
    out->uWind = -1;
    if (!sAvailable)
        return false;

    const GLuint v = CompileStage(GL_VERTEX_SHADER, name, vs);
    const GLuint f = (v != 0) ? CompileStage(GL_FRAGMENT_SHADER, name, fs) : 0;
    if (f == 0)
    {
        if (v != 0)
            sGl.DeleteShader(v);
        return false;
    }
    const GLuint p = sGl.CreateProgram();
    GLint linked = 0;
    sGl.AttachShader(p, v);
    sGl.AttachShader(p, f);
    sGl.LinkProgram(p);
    sGl.DeleteShader(v); // attached shaders are only flagged; freed with the program
    sGl.DeleteShader(f);
    sGl.GetProgramiv(p, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[1024];
        GLsizei n = 0;
        sGl.GetProgramInfoLog(p, (GLsizei)sizeof(log), &n, log);
        DeckGl_Log("[SHADER] %s link FAILED:\n%.*s\n", name, (int)n, log);
        sGl.DeleteProgram(p);
        return false;
    }
    out->program = p;
    out->uTime = sGl.GetUniformLocation(p, "uTime");
    out->uWind = sGl.GetUniformLocation(p, "uWind");
    DeckGl_Log("[SHADER] %s: linked (program %u)\n", name, (unsigned)p);
    return true;
}

void DeckGl_DeleteProgram(DeckGlProgram *p)
{
    if (p->program != 0 && sAvailable)
        sGl.DeleteProgram(p->program);
    p->program = 0;
}

void DeckGl_UseProgram(const DeckGlProgram *p)
{
    if (!sAvailable)
        return;
    if (p == NULL || p->program == 0)
    {
        sGl.UseProgram(0);
        return;
    }
    sGl.UseProgram(p->program);
    if (p->uTime >= 0)
        sGl.Uniform1f(p->uTime, sTime);
    if (p->uWind >= 0)
        sGl.Uniform3f(p->uWind, sWind[0], sWind[1], sWind[2]);
}

GLint DeckGl_Uniform(const DeckGlProgram *p, const char *name)
{
    return (sAvailable && p->program != 0) ? sGl.GetUniformLocation(p->program, name) : -1;
}

void DeckGl_Set1i(GLint loc, int v) { if (loc >= 0) sGl.Uniform1i(loc, v); }
void DeckGl_Set1f(GLint loc, float v) { if (loc >= 0) sGl.Uniform1f(loc, v); }
void DeckGl_Set2f(GLint loc, float a, float b) { if (loc >= 0) sGl.Uniform2f(loc, a, b); }
void DeckGl_Set3f(GLint loc, float a, float b, float c) { if (loc >= 0) sGl.Uniform3f(loc, a, b, c); }
void DeckGl_Set4f(GLint loc, float a, float b, float c, float d) { if (loc >= 0) sGl.Uniform4f(loc, a, b, c, d); }

void DeckGl_SetMat4(GLint loc, const float *m16) { if (loc >= 0) sGl.UniformMatrix4fv(loc, 1, GL_FALSE, m16); }

void DeckGl_ActiveTexture(GLenum unit)
{
    if (sAvailable)
        sGl.ActiveTexture(unit);
}

void DeckGl_SetFrameUniforms(float timeSeconds, float windX, float windZ, float windStrength)
{
    sTime = timeSeconds;
    sWind[0] = windX;
    sWind[1] = windZ;
    sWind[2] = windStrength;
}

bool DeckGl_SelfTest(void)
{
    static const char kVs[] =
        "#version 120\n"
        "void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; gl_FrontColor = gl_Color; }\n";
    static const char kFs[] =
        "#version 120\n"
        "uniform float uTime;\n"
        "void main() { gl_FragColor = gl_Color * (0.5 + 0.5 * sin(uTime)); }\n";
    DeckGlProgram p;
    if (!DeckGl_Init() || !DeckGl_BuildProgram(&p, "selftest", kVs, kFs))
    {
        DeckGl_Log("[SHADER] self-test FAILED\n");
        return false;
    }
    DeckGl_DeleteProgram(&p);
    DeckGl_Log("[SHADER] self-test OK\n");
    const char *dbg = getenv("DECK_GL_DEBUG");
    if (dbg != NULL && strcmp(dbg, "0") != 0)
    {
        // one-shot timer exercise: query 0 is read back when query 2 begins, both slots get used
        const bool on = DeckGl_TimerInit();
        for (int i = 0; on && i < 3; i++)
        {
            DeckGl_TimerBegin();
            glClear(GL_COLOR_BUFFER_BIT);
            DeckGl_TimerEnd();
            glFinish();
        }
        DeckGl_Log("[SHADER] timer query self-test: %s, last result %.4f ms\n", on ? "ran" : "unavailable", DeckGl_TimerLastMs());
        DeckGl_TimerShutdown();
    }
    return true;
}

bool DeckGl_CreateTarget(DeckGlTarget *t, int w, int h, DeckGlDepth depth, GLint filter)
{
    static const char *const kDepthNames[] = { "none", "texture24", "renderbuffer24" };
    GLint prev = 0, prevTex = 0, prevUnit = 0, prevRb = 0;
    memset(t, 0, sizeof(*t));
    t->w = w;
    t->h = h;
    t->depth = depth;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);   // of the active unit; this function never switches unit
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevUnit);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRb);

    glGenTextures(1, &t->color);
    glBindTexture(GL_TEXTURE_2D, t->color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glGenFramebuffers(1, &t->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->color, 0);
    if (depth == DECK_GL_DEPTH_TEXTURE24)
    {
        glGenTextures(1, &t->depthTex);
        glBindTexture(GL_TEXTURE_2D, t->depthTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t->depthTex, 0);
    }
    else if (depth == DECK_GL_DEPTH_RENDERBUFFER24)
    {
        glGenRenderbuffers(1, &t->depthRb);
        glBindRenderbuffer(GL_RENDERBUFFER, t->depthRb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t->depthRb);
    }
    const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    DeckGl_ActiveTexture((GLenum)prevUnit);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)prevRb);
    t->complete = (st == GL_FRAMEBUFFER_COMPLETE);
    DeckGl_Log("[FBO] %dx%d RGBA8 depth=%s: status 0x%04x %s\n", w, h, kDepthNames[depth],
               (unsigned)st, t->complete ? "COMPLETE" : "INCOMPLETE");
    return t->complete;
}

void DeckGl_DeleteTarget(DeckGlTarget *t)
{
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    if (t->color) glDeleteTextures(1, &t->color);
    if (t->depthTex) glDeleteTextures(1, &t->depthTex);
    if (t->depthRb) glDeleteRenderbuffers(1, &t->depthRb);
    memset(t, 0, sizeof(*t));
}

bool DeckGl_TimerInit(void)
{
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    bool ok = true;
    if (sTimerOn)
        return true; // idempotent: a second init must not leak the first pair of queries
    if (!sAvailable || ext == NULL || strstr(ext, "GL_ARB_timer_query") == NULL)
    {
        DeckGl_Log("[SHADER] GPU timer off (GL_ARB_timer_query not listed)\n");
        return false;
    }
    DECK_GL_LOAD(GenQueries, "glGenQueries");
    DECK_GL_LOAD(DeleteQueries, "glDeleteQueries");
    DECK_GL_LOAD(BeginQuery, "glBeginQuery");
    DECK_GL_LOAD(EndQuery, "glEndQuery");
    DECK_GL_LOAD(GetQueryObjectiv, "glGetQueryObjectiv");
    DECK_GL_LOAD(GetQueryObjectui64v, "glGetQueryObjectui64v");
    if (!ok)
        return false;
    sGl.GenQueries(2, sTimerQ);
    sTimerUsed[0] = sTimerUsed[1] = false;
    sTimerIdx = 0;
    sTimerLastMs = -1.0;
    sTimerActive = false;
    sTimerOn = true;
    return true;
}

void DeckGl_TimerShutdown(void)
{
    if (sTimerOn)
    {
        if (sTimerActive)
            sGl.EndQuery(GL_TIME_ELAPSED);
        sGl.DeleteQueries(2, sTimerQ);
    }
    sTimerOn = false;
    sTimerActive = false;
}

void DeckGl_TimerBegin(void)
{
    if (!sTimerOn || sTimerActive)
        return; // Begin twice without End is ignored (GL would raise GL_INVALID_OPERATION)
    const unsigned i = sTimerIdx & 1u;
    if (sTimerUsed[i])
    {
        GLint ready = 0;
        sGl.GetQueryObjectiv(sTimerQ[i], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready)
        {
            GLuint64 ns = 0;
            sGl.GetQueryObjectui64v(sTimerQ[i], GL_QUERY_RESULT, &ns);
            sTimerLastMs = (double)ns / 1.0e6;
        }
        // not ready: keep the last good result and never stall the frame waiting for it
    }
    sGl.BeginQuery(GL_TIME_ELAPSED, sTimerQ[i]);
    sTimerActive = true;
}

void DeckGl_TimerEnd(void)
{
    if (!sTimerOn || !sTimerActive)
        return; // End without Begin is ignored
    sGl.EndQuery(GL_TIME_ELAPSED);
    sTimerActive = false;
    sTimerUsed[sTimerIdx & 1u] = true;
    sTimerIdx++;
}

double DeckGl_TimerLastMs(void)
{
    return sTimerOn ? sTimerLastMs : -1.0;
}
