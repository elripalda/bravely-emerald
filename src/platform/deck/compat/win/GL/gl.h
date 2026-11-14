#ifndef DECK_COMPAT_WIN_GL_H
#define DECK_COMPAT_WIN_GL_H

// Windows build: opengl32.dll exports OpenGL 1.1 only. Everything newer (buffers, framebuffers,
// mipmaps, programs) is loaded at run time through SDL_GL_GetProcAddress by WinGl_Load()
// (win_gl.c, called once the context exists). The calls in the code keep their gl names: each is
// a macro for a function pointer, so no source file has to change. This header sits first on the
// include path of the Windows build and hands over to MinGW's own GL/gl.h.

// MinGW's gl.h includes <windows.h> unless these two are already defined; windows.h would clash
// with the game's own names (ScrollWindow, abs, ...). Define them here, as windows.h would.
#ifndef WINGDIAPI
#define WINGDIAPI __declspec(dllimport)
#endif
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#ifndef GLAPI
#define GLAPI extern
#endif
#include_next <GL/gl.h>
#include <GL/glext.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WINGL_FUNCS(X)                                                                  \
    X(PFNGLBINDBUFFERPROC, BindBuffer)                                                  \
    X(PFNGLGENBUFFERSPROC, GenBuffers)                                                  \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers)                                            \
    X(PFNGLBUFFERDATAPROC, BufferData)                                                  \
    X(PFNGLBUFFERSUBDATAPROC, BufferSubData)                                            \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer)                                        \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers)                                        \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)                                  \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D)                              \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus)                          \
    X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer)                                      \
    X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers)                                      \
    X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers)                                \
    X(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage)                                \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer)                        \
    X(PFNGLGENERATEMIPMAPPROC, GenerateMipmap)                                          \
    X(PFNGLUSEPROGRAMPROC, UseProgram)                                                  \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture)                                            \
    X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate)

#define WINGL_DECLARE(type, name) extern type WinGl_##name;
WINGL_FUNCS(WINGL_DECLARE)
#undef WINGL_DECLARE

#define glBindBuffer WinGl_BindBuffer
#define glGenBuffers WinGl_GenBuffers
#define glDeleteBuffers WinGl_DeleteBuffers
#define glBufferData WinGl_BufferData
#define glBufferSubData WinGl_BufferSubData
#define glBindFramebuffer WinGl_BindFramebuffer
#define glGenFramebuffers WinGl_GenFramebuffers
#define glDeleteFramebuffers WinGl_DeleteFramebuffers
#define glFramebufferTexture2D WinGl_FramebufferTexture2D
#define glCheckFramebufferStatus WinGl_CheckFramebufferStatus
#define glBindRenderbuffer WinGl_BindRenderbuffer
#define glGenRenderbuffers WinGl_GenRenderbuffers
#define glDeleteRenderbuffers WinGl_DeleteRenderbuffers
#define glRenderbufferStorage WinGl_RenderbufferStorage
#define glFramebufferRenderbuffer WinGl_FramebufferRenderbuffer
#define glGenerateMipmap WinGl_GenerateMipmap
#define glUseProgram WinGl_UseProgram
#define glActiveTexture WinGl_ActiveTexture
#define glBlendFuncSeparate WinGl_BlendFuncSeparate

// Loads every pointer above; returns how many are missing (0 = all present). GL thread only.
int WinGl_Load(void);

#ifdef __cplusplus
}
#endif

#endif
